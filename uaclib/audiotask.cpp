/*!
#
# Win-Widget. Windows related software for Audio-Widget/SDR-Widget (http://code.google.com/p/sdr-widget/)
# Copyright (C) 2012 Nikolay Kovbasa
#
# Permission to copy, use, modify, sell and distribute this software 
# is granted provided this copyright notice appears in all copies. 
# This software is provided "as is" without express or implied
# warranty, and with no claim as to its suitability for any purpose.
#
#----------------------------------------------------------------------------
# Contact: nikkov@gmail.com
#----------------------------------------------------------------------------
*/

#include "USBAudioDevice.h"
#include "audiotask.h"

HANDLE		AudioTask::m_s_RxDataEvent = NULL;
int			AudioTask::m_s_LastLen = 0;

#define NEXT_INDEX(x)		((x + 1) % (sizeof(m_isoBuffers) / sizeof(ISOBuffer)))

#define MAX_OVL_ERROR_COUNT	3
#define OVL_WAIT_TIMEOUT	100


bool AudioTask::BeforeStart()
{
	if(m_DataBufferSize == 0 || m_packetPerTransfer == 0 || m_packetSize == 0)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. Can't start AudioTask thread: unknown sample freq\n", TaskName());
#endif
		return FALSE;
	}
	if(!m_isoBuffers[0].DataBuffer)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. Can't start AudioTask thread: buffers not allocated\n", TaskName());
#endif
		return FALSE;
	}

	m_SubmittedCount = 0;
	m_CompletedCount = 0;

	// set first frame 100ms in the future
	m_FrameNumber = m_device->GetCurrentFrameNumber() + 40;
	m_LastStartFrame = 0;
	m_isoTransferErrorCount = 0;

	bool r = m_device->OvlInit(&m_OvlPool, MAX_OUTSTANDING_TRANSFERS);
	if(!r)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. Can't start AudioTask thread: OvlK_Init failed\n", TaskName());
#endif
		return FALSE;
	}
	for(int i = 0; i < sizeof(m_isoBuffers) / sizeof(ISOBuffer); i++)
	{
		ISOBuffer* bufferEL = m_isoBuffers + i;
		memset(bufferEL->DataBuffer, 0xAA, m_DataBufferSize);
		//memset(bufferEL->DataBuffer, 0, m_DataBufferSize);
        IsoK_SetPackets(bufferEL->IsoContext, m_packetSize);
        bufferEL->IsoPackets = bufferEL->IsoContext->IsoPackets;
        m_device->OvlAcquire(&bufferEL->OvlHandle, m_OvlPool);
	}
	m_outstandingIndex = 0;
	m_completedIndex = 0;

    // Reset the pipe.
    r = m_device->UsbResetPipe((UCHAR)m_pipeId);

	BeforeStartInternal();
	m_isStarted = TRUE;
#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: %s. Before start thread is OK\n", TaskName());
	m_sampleNumbers = 0;
	m_tickCount = 0;
#endif
	return TRUE;
}

bool AudioTask::AfterStop()
{
	if(!m_isStarted)
		return TRUE;

	m_device->UsbAbortPipe((UCHAR)m_pipeId);

	//index of last buffer in queue
	m_outstandingIndex;
	//index of last received buffer
	m_completedIndex;

    //  Cancel all transfers left outstanding.
#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: %s. Cancel outstanding transfers\n", TaskName());
#endif
    while(m_completedIndex != m_outstandingIndex)
    {
        ISOBuffer* nextBufferEL = m_isoBuffers + m_completedIndex;
        UINT transferred;
		m_device->OvlWaitOrCancel(nextBufferEL->OvlHandle, 0, &transferred);
		m_completedIndex = NEXT_INDEX(m_completedIndex);
    }
	m_device->ClearErrorCode();
	for(int i = 0; i < sizeof(m_isoBuffers) / sizeof(ISOBuffer); i++)
	{
		//  Free the iso buffer resources.
		ISOBuffer* bufferEL = m_isoBuffers + i;
		m_device->OvlRelease(bufferEL->OvlHandle);
	}
    // Free the overlapped pool.
    OvlK_Free(m_OvlPool);
	m_isStarted = FALSE;
	AfterStopInternal();
#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: %s. After stop thread is OK\n", TaskName());
#endif

	return TRUE;
}

bool AudioTask::FreeBuffers()
{
    //  Free the iso buffer resources.
	if(m_isoBuffers[0].DataBuffer != NULL)
		for(int i = 0; i < sizeof(m_isoBuffers) / sizeof(ISOBuffer); i++)
		{
			ISOBuffer* nextBufferEL = m_isoBuffers + i;
			IsoK_Free(nextBufferEL->IsoContext);
			nextBufferEL->IsoContext = NULL;
			delete nextBufferEL->DataBuffer;
			nextBufferEL->DataBuffer = NULL;
		}
	m_outstandingIndex = 0;
	m_completedIndex = 0;
#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: %s. Free buffers is OK\n", TaskName());
#endif
	return TRUE;
}

bool AudioTask::AllocBuffers()
{
	if(m_packetPerTransfer == 0 || m_packetSize == 0)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. Can't start AllocBuffers: unknown sample freq\n", TaskName());
#endif
		return FALSE;
	}
	m_DataBufferSize = m_packetPerTransfer * m_packetSize;

    for (int i = 0; i < sizeof(m_isoBuffers) / sizeof(ISOBuffer); i++)
    {
        ISOBuffer* bufferEL = m_isoBuffers + i;
        bufferEL->DataBuffer = new UCHAR[m_DataBufferSize];
        memset(bufferEL->DataBuffer, 0xAA, m_DataBufferSize);
        //memset(bufferEL->DataBuffer, 0, m_DataBufferSize);
        IsoK_Init(&bufferEL->IsoContext, m_packetPerTransfer, 0);
        IsoK_SetPackets(bufferEL->IsoContext, m_packetSize);
    }

#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. AllocBuffers OK\n", TaskName());
#endif
		return TRUE;
}

bool AudioTask::Work(volatile TaskState& taskState)
{
	ISOBuffer* nextXfer;
	UINT transferred;
	int dataLength = 0;

	m_buffersGuard.Enter();
	while(taskState == TaskStarted && NEXT_INDEX(m_outstandingIndex) != m_completedIndex && m_device->GetErrorCode() == ERROR_SUCCESS)
	{
		nextXfer = m_isoBuffers + m_outstandingIndex;
		dataLength = FillBuffer(nextXfer);
		m_outstandingIndex = NEXT_INDEX(m_outstandingIndex);
		m_device->OvlReUse(nextXfer->OvlHandle);
		SetNextFrameNumber(nextXfer);

		RWBuffer(nextXfer, dataLength);
	}

		// enable output after first requests have been issued
	


	//find next waiting buffer in queue
	nextXfer = m_isoBuffers + m_completedIndex;
	if (!nextXfer || taskState != TaskStarted) 
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. No more packets!\n", TaskName());
#endif
		return TRUE;
	}

	if(!m_device->OvlWait(nextXfer->OvlHandle, OVL_WAIT_TIMEOUT, KOVL_WAIT_FLAG_NONE, &transferred))
//	if(!m_device->OvlWaitOrCancel(nextXfer->OvlHandle, OVL_WAIT_TIMEOUT, &transferred))
	{
		int deviceErrorCode = m_device->GetErrorCode();
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s OvlK_Wait failed. ErrorCode: %08Xh\n", TaskName(), deviceErrorCode);
#endif
		m_isoTransferErrorCount++;
		if(//deviceErrorCode == ERROR_GEN_FAILURE ||
			m_isoTransferErrorCount >= MAX_OVL_ERROR_COUNT)
		{
#ifdef _ENABLE_TRACE
			debugPrintf("ASIOUAC: Notify to device about error\n"); //report to device error
#endif
			m_device->Notify(0);
			// if we fail to wait,then sleep a little so we don't use all the CPU
			Sleep(10);
			return FALSE;
		}
		else
			m_device->ClearErrorCode();
	}
	else
	{
		ProcessBuffer(nextXfer);
		m_isoTransferErrorCount = 0; //reset error count
		if (0 == m_CompletedCount)
			AfterPrime();
	}


	IsoXferComplete(nextXfer, transferred);
#ifdef _ENABLE_TRACE
	CalcStatistics(transferred);
#endif
	m_completedIndex = NEXT_INDEX(m_completedIndex);
	m_buffersGuard.Leave();

	return TRUE;
}



bool AudioDACTask::BeforeStartInternal()
{
	UCHAR policyValue = 1;

	m_RemainingBytes = 0;
	m_TotalWrites = 0;

	if(m_feedbackInfo)
	{
		m_feedbackInfo->ClearStatistics();
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. Clear feedback statistics\n", TaskName());
#endif
		m_feedbackInfo->SetIntervalValue((float)(8 / (1 << (m_interval - 1))));
//		m_feedbackInfo->SetValue(0);
		m_feedbackInfo->SetDefaultValue(m_defaultPacketSize);
	}
#ifdef _ENABLE_TRACE
		m_sampleNumbers = 0;
		m_tickCount = 0;
#endif
	

	//m_device->UsbSetPipePolicy((UCHAR)m_pipeId, RAW_IO, 1, &policyValue);
	policyValue = 1;
	m_device->UsbSetPipePolicy((UCHAR)m_pipeId, ISO_ALWAYS_START_ASAP, 1, &policyValue);
	return TRUE; 
		//m_device->UsbSetPipePolicy((UCHAR)m_pipeId, ISO_ALWAYS_START_ASAP, 1, &policyValue) &&
		//m_device->UsbSetPipePolicy((UCHAR)m_pipeId, RESET_PIPE_ON_RESUME, 1, &policyValue); //experimental
}

void AudioDACTask::AfterPrime()
{
	Sleep(30); //wait for data to get to buffer
	#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s enabling output (TotalSends %d)\n", TaskName(), m_TotalWrites);
#endif
	m_device->EnableOutput();
}


bool AudioDACTask::AfterStopInternal()
{
#ifdef _ENABLE_TRACE
	if(m_feedbackInfo)
		debugPrintf("ASIOUAC: %s. Maximum feedback value (%f), minimum feedback value (%f)\n", TaskName(),  m_feedbackInfo->GetMaxValue(), m_feedbackInfo->GetMinValue());
#endif
	return TRUE;
}

int AudioDACTask::FillBuffer(ISOBuffer* nextXfer)
{
	int dataLength = 0;
	int OutLen = m_s_LastLen;

	if (m_TotalWrites > MAX_OUTSTANDING_TRANSFERS)
	{
		DWORD Res = WaitForSingleObject(m_s_RxDataEvent, 100);
	}
	else
	{
		OutLen = (m_channelNumber * m_sampleSize * nextXfer->IsoContext->NumberOfPackets * m_defaultPacketSize);
	}
	if (m_TotalWrites < 1000)
	{
		m_TotalWrites++;
	}

	if (OutLen)
	{
		OutLen += m_RemainingBytes;

		double BytesPerPacket = (double)OutLen/(double)nextXfer->IsoContext->NumberOfPackets;

		int SmallPacketSize = ((int)BytesPerPacket)/(m_channelNumber * m_sampleSize);
		SmallPacketSize *= (m_channelNumber * m_sampleSize);
		int LargePacketSize = SmallPacketSize + (m_channelNumber * m_sampleSize);
		double BytesSoFar = 0;

		int nextOffSet = 0;
		for (int packetIndex = 0; packetIndex < nextXfer->IsoContext->NumberOfPackets; packetIndex++)
		{
			nextXfer->IsoContext->IsoPackets[packetIndex].Offset = nextOffSet;
			
			BytesSoFar += BytesPerPacket;

			if (BytesSoFar > nextOffSet + LargePacketSize)
				nextOffSet += LargePacketSize;
			else
				nextOffSet += SmallPacketSize;
		}

#ifdef _ENABLE_TRACE
//		debugPrintf("ASIOUAC: %s. LastLen %d remainingbytes %d outlen %d total %d\n", TaskName(), m_s_LastLen, m_RemainingBytes, OutLen, nextOffSet);
#endif

		m_RemainingBytes = OutLen - nextOffSet;

//
//	float raw_cur_feedback = m_defaultPacketSize*(1.000 + (-0.03/(float)nextXfer->IsoContext->NumberOfPackets)); //1.00000;// - 0.0007;
//		
////		m_feedbackInfo == NULL || m_feedbackInfo->GetValue() == 0.0f 
////						?  m_defaultPacketSize
////						:  m_feedbackInfo->GetValue(); //value in stereo samples in one second // BSB: or in one milisecond??
//
//	int maxSamplesInPacket = m_packetSize / m_channelNumber / m_sampleSize; //max stereo samples in one packet
//	if(raw_cur_feedback > (float)(maxSamplesInPacket))
//	{
//#ifdef _ENABLE_TRACE
//		debugPrintf("ASIOUAC: %s. Feedback value (%f) larger than the maximum packet size\n", TaskName(), raw_cur_feedback);
//#endif
//		raw_cur_feedback = (float)maxSamplesInPacket;
//	}
//	int dataLength = 0;
//	if(raw_cur_feedback > 0)
//	{
//		//raw_cur_feedback = m_defaultPacketSize;
//		//in one second we have 8 / (1 << (m_interval - 1)) packets
//		//one packet must contain samples number = [cur_feedback * (1 << (m_interval - 1)) / 8]
//		float cur_feedback = raw_cur_feedback; //number stereo samples in one packet
//		int icur_feedback = (int)(cur_feedback + 0.5f); // BSB: added +0.5f to get round() function. (int)(f) === floor(f), f>=0
//		int nextOffSet = 0;
//		static float addSample = 0; // BSB: added static.
//		float frac = cur_feedback - icur_feedback;
//		if(raw_cur_feedback == (float)maxSamplesInPacket) 
//		{
//			frac = 0.f;
//			addSample = 0;
//		}
//		
//		//frac += 0.1f;
//
//		icur_feedback *= m_channelNumber * m_sampleSize;
//
//		if (AddOne > 0)
//		{
//#ifdef _ENABLE_TRACE
//			debugPrintf("+\n");
//#endif
//
//			addSample += 1.0;
//			AddOne = 0;
//		}
//
//
//		for (int packetIndex = 0; packetIndex < nextXfer->IsoContext->NumberOfPackets; packetIndex++)
//		{
//			nextXfer->IsoContext->IsoPackets[packetIndex].Offset = nextOffSet;
//			nextOffSet += icur_feedback;
//			addSample += frac;
//			if(addSample > 0.5f) // 1.f)
//			{
//				nextOffSet += m_channelNumber * m_sampleSize; //append additional stereo sample
//				addSample -= 1.f;
//			}
//			else if(addSample < -0.5f) // -1.f)	// BSB: Added negative case
//			{
//				nextOffSet -= m_channelNumber * m_sampleSize; //append additional stereo sample
//				addSample += 1.f;
//			}
////			nextXfer->IsoContext->IsoPackets[packetIndex].Length = nextOffSet - nextXfer->IsoContext->IsoPackets[packetIndex].Offset;
//		}


		dataLength = (int)nextOffSet;

		if (m_readDataCb && (m_TotalWrites > MAX_OUTSTANDING_TRANSFERS))
		{
			m_readDataCb(m_readDataCbContext, nextXfer->DataBuffer, dataLength);
		}
		else
		{
			memset( nextXfer->DataBuffer, 0, dataLength);
		}

#ifdef _ENABLE_TRACE
		//You can dump data for analise like this
		if(m_dumpFile) { 
			char packetstart[] = {'*', '*', 0x00, 0x80, 'P', 'a', 'c', 'k', 'e', 't', ' ', 's', 't', 'a', 'r', 't'}; // BSB added packet identifier 
			fwrite(packetstart, 1, 16, m_dumpFile); // "**", L: negative 16-bit fullscalle, "Packet start"
			fwrite(nextXfer->DataBuffer, 1, dataLength, m_dumpFile);
		}
#endif

#ifdef _ENABLE_TRACE
//		debugPrintf("ASIOUAC: %s. Transfer: feedback val = %.1f, send %.1f samples, transfer length=%d\n", TaskName(), raw_cur_feedback, (float)dataLength/8.f, dataLength);
#endif
	}
	else
	{
		//default transfer length
		dataLength = (int)(m_defaultPacketSize * m_packetPerTransfer)*(m_channelNumber * m_sampleSize);
		memset(nextXfer->DataBuffer, 0, dataLength);
	}

	
	//dataLength = 512*m_packetPerTransfer; //(int)(m_defaultPacketSize * m_packetPerTransfer)*(m_channelNumber * m_sampleSize);
	return dataLength;
}

bool AudioDACTask::RWBuffer(ISOBuffer* nextXfer, int len)
{
	if(!m_device->UsbIsoWritePipe(m_pipeId, nextXfer->DataBuffer, len, (LPOVERLAPPED)nextXfer->OvlHandle, nextXfer->IsoContext))
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. IsoWritePipe failed. ErrorCode: %08Xh\n", TaskName(),  m_device->GetErrorCode());
#endif
		return FALSE;
	}
	return TRUE;
}

void AudioDACTask::ProcessBuffer(ISOBuffer* buffer)
{
}

#ifdef _ENABLE_TRACE
void AudioDACTask::CalcStatistics(int sampleNumbers)
{
	if(m_tickCount == 0)
	{
		m_tickCount = m_firstTickCount = GetTickCount();
		m_sampleNumbers = 0;
	}
	else
	{
		DWORD curTick = GetTickCount();
		m_sampleNumbers += (double)sampleNumbers / ((double)m_channelNumber * m_sampleSize);
		if(curTick - m_tickCount > 5000)
		{
			if(m_feedbackInfo == NULL)
				debugPrintf("ASIOUAC: %s. Current sample freq: %f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f);
			else
				debugPrintf("ASIOUAC: %s. Current sample freq: %f (interval %d), by fb=%f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f, curTick - m_firstTickCount, m_feedbackInfo->GetFreqValue());
//			m_sampleNumbers = 0;
			m_tickCount = curTick;
		}
	}
}
#endif

bool AudioADCTask::BeforeStartInternal()
{
	m_s_LastLen = 0;
	if(m_feedbackInfo != NULL)
		//m_feedbackInfo->SetValue(0);
		m_feedbackInfo->SetDefaultValue(m_defaultPacketSize);
	//return TRUE;
#ifdef _ENABLE_TRACE
		m_sampleNumbers = 0;
		m_tickCount = 0;
#endif

	m_s_RxDataEvent = CreateEvent( NULL, true, false, NULL );
	m_StartedFlow = false;

	UCHAR policyValue = 1;
	return m_device->UsbSetPipePolicy((UCHAR)m_pipeId, ISO_ALWAYS_START_ASAP, 1, &policyValue);
}

void AudioADCTask::AfterPrime()
{
	m_device->EnableRx();
}


bool AudioADCTask::AfterStopInternal()
{
	return TRUE;
}



int AudioADCTask::FillBuffer(ISOBuffer* nextXfer)
{
	return m_packetPerTransfer * m_packetSize;
}

bool AudioADCTask::RWBuffer(ISOBuffer* nextXfer, int len)
{
	if(!m_device->UsbIsoReadPipe(m_pipeId, nextXfer->DataBuffer, len, (LPOVERLAPPED)nextXfer->OvlHandle, nextXfer->IsoContext))
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. IsoReadPipe (ADC) failed. ErrorCode: %08Xh\n", TaskName(), m_device->GetErrorCode());
#endif
		return FALSE;
	}
	return TRUE;
}

#define PACK_ADC_BUFFER

void AudioADCTask::ProcessBuffer(ISOBuffer* buffer)
{
	int packetLength = 0;
	int recLength = 0;
	for(int i = 0; i < buffer->IsoContext->NumberOfPackets; i++)
	{
		if (buffer->IsoContext->IsoPackets[i].Status == 0)
		{
			packetLength = buffer->IsoContext->IsoPackets[i].Length;
			UCHAR *Src = (buffer->DataBuffer + buffer->IsoPackets[i].Offset);
			UCHAR *Dest = ((UCHAR*) &BCABuff) + BCABuffHead;
			
			int Len = packetLength;
			
			if (BCABuffHead + packetLength > sizeof(BCABuff))
			{
				Len = sizeof(BCABuff) - BCABuffHead;
				memcpy( Dest, Src, Len );
				Src +=  Len;
				BCABuffHead = (BCABuffHead+Len) % (sizeof(BCABuff));
				Dest = ((UCHAR*) &BCABuff) + BCABuffHead;
				BCABuffCount += Len;
				Len = packetLength - Len;
			}

			memcpy( Dest, Src, Len );
			BCABuffHead = (BCABuffHead+Len) % (sizeof(BCABuff));
			BCABuffCount += Len;

			recLength += packetLength;
		}
		else
		{
			packetLength = buffer->IsoContext->IsoPackets[i].Length;
		}
	}

	

	if ((recLength >= m_packetPerTransfer*m_channelNumber*m_sampleSize*m_defaultPacketSize) || m_StartedFlow)
	{
		m_StartedFlow = true;
		m_s_LastLen = recLength;
		SetEvent(m_s_RxDataEvent);
	}

#ifdef PACK_ADC_BUFFER
	if(m_writeDataCb)
	{
		if (BCABuffCount > 4000)
		{
			if (BCABuffTail > BCABuffHead)
			{
				UCHAR *Src = ((UCHAR*) &BCABuff) + BCABuffTail;
				int Count = ((sizeof(BCABuff)) - BCABuffTail)/(8*4);
				Count *= (8*4);
				m_writeDataCb(m_writeDataCbContext, Src, Count);
				BCABuffTail = (BCABuffTail+Count) % (sizeof(BCABuff));
				BCABuffCount -= Count;
			}

			if (BCABuffCount)
			{
				UCHAR *Src = ((UCHAR*) &BCABuff) + BCABuffTail;
				int Count = BCABuffCount/(8*4);
				Count *= (8*4);
				m_writeDataCb(m_writeDataCbContext, (UCHAR *)Src, Count);
				BCABuffTail = (BCABuffTail+(Count)) % (sizeof(BCABuff));
				BCABuffCount -= Count;
			}
			
		}
	}
#endif
	if(m_feedbackInfo && m_StartedFlow)
	{
/*
		int div = buffer->IsoContext->NumberOfPackets * m_channelNumber * m_sampleSize * (1 << (m_interval - 1)) / 8;
		int d1 = recLength / div;
		int d2 = recLength % div;
		m_feedbackInfo->SetValue((d1 << 14) + d2);
*/
		int div = buffer->IsoContext->NumberOfPackets * m_channelNumber * m_sampleSize;
		int d1 = recLength / div;
		int d2 = recLength % div;
		m_feedbackInfo->SetValue((d1 << 16) + d2);
		//m_feedbackInfo->SetValue(recLength);
	}
}


#ifdef _ENABLE_TRACE
void AudioADCTask::CalcStatistics(int sampleNumbers)
{
	if(m_tickCount == 0)
	{
		m_tickCount = m_firstTickCount = GetTickCount();
		m_sampleNumbers = 0;
	}
	else
	{
		DWORD curTick = GetTickCount();
		m_sampleNumbers += (double)sampleNumbers / ((double)m_channelNumber * m_sampleSize);
		if(curTick - m_tickCount > 5000)
		{
			if(m_feedbackInfo == NULL)
				debugPrintf("ASIOUAC: %s. Current sample freq: %f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f);
			else
				debugPrintf("ASIOUAC: %s. Current sample freq: %f (interval %d), by fb=%f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f, curTick - m_firstTickCount, m_feedbackInfo->GetFreqValue());
//			m_sampleNumbers = 0;
			m_tickCount = curTick;
		}
	}

}
#endif


int AudioFeedbackTask::FillBuffer(ISOBuffer* nextXfer)
{
	return 64;
}

bool AudioFeedbackTask::RWBuffer(ISOBuffer* nextXfer, int len)
{
	if(!m_device->UsbReadPipe(m_pipeId, nextXfer->DataBuffer, len, (LPOVERLAPPED)nextXfer->OvlHandle))
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: %s. IsoReadPipe (feedback) failed. ErrorCode: %08Xh\n", TaskName(), m_device->GetErrorCode());
#endif
		return FALSE;
	}
	return TRUE;
}

void AudioFeedbackTask::ProcessBuffer(ISOBuffer* nextXfer)
{
	if(m_feedbackInfo == NULL)
		return;


	unsigned short *Count = (unsigned short *) &nextXfer->DataBuffer[18];

	for (int i = 0; i < 64; i++)
	{
		if (nextXfer->DataBuffer[i] != MyFeedback[i])
		{
#ifdef _ENABLE_TRACE
			if ((i != 18)/* && (i != 19)*/)
				debugPrintf("Feedback: %d: %02.2X->%02.2X. \n", i, MyFeedback[i], nextXfer->DataBuffer[i]);
			else
			{
				if (MyFeedback[i]+1 != nextXfer->DataBuffer[i])
				{
					debugPrintf("Feedback: %d: %02.2X->%02.2X. \n", i, MyFeedback[i], nextXfer->DataBuffer[i]);
				}
			}
#endif
		}
	}

	memcpy( MyFeedback, nextXfer->DataBuffer, 64 );


	if (MyFeedback[16] == 0xff)
	{
		if (m_dac)
		{
//			m_dac->IncrementAddOne();
		}
	}

#ifdef _ENABLE_TRACE
	//debugPrintf("F*");
#endif

}


bool AudioFeedbackTask::BeforeStartInternal()
{
	memset( MyFeedback, 0, sizeof(MyFeedback));
	//if(m_feedbackInfo != NULL)
	//	m_feedbackInfo->SetValue(0);
	return TRUE;
}

void AudioFeedbackTask::AfterPrime()
{
}



bool AudioFeedbackTask::AfterStopInternal()
{
	return TRUE;
}



#ifdef _ENABLE_TRACE
void AudioFeedbackTask::CalcStatistics(int sampleNumbers)
{
	if(m_tickCount == 0)
	{
		m_tickCount = m_firstTickCount = GetTickCount();
		m_sampleNumbers = 0;
		m_AveDiff = 0;
	}
	else
	{
		DWORD curTick = GetTickCount();
		m_sampleNumbers ++;

		if (m_dac->GetTotal() == 0)
		{
			m_sampleNumbers = 0;
			m_AveDiff = 0;
		}
		__int64 TotalOutput = m_dac->GetTotal();
		__int64 TotalRequired = m_sampleNumbers * (3072*2); //(96000.0/3072.0/2.0);

		__int64 TotalDiff = TotalRequired - TotalOutput;
		
		m_AveDiff = m_AveDiff * 0.99;
		m_AveDiff = m_AveDiff + (double)(TotalDiff)*0.01;

		if (m_AveDiff < -8*4)
		{
			m_dac->IncrementAddOne();
		}

		if(curTick - m_tickCount > 5000)
		{
			if(m_feedbackInfo == NULL)
				debugPrintf("ASIOUAC: %s. Current sample freq: %f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f);
			else
				debugPrintf("ASIOUAC: %s. Current sample freq: %f (interval %d), samples/feedback=%f diff=%d ave %f\n", TaskName(), (float)m_sampleNumbers / (float)(curTick - m_firstTickCount) * 1000.f, curTick - m_firstTickCount, ((float)m_dac->GetTotal())/(float)m_sampleNumbers, (int)TotalDiff, (float)m_AveDiff);
//			m_sampleNumbers = 0;
			m_tickCount = curTick;
		}
	}

}
#endif
