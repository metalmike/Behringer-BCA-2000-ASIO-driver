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
/*
	This code based on samples from library LibUsbK by Travis Robinson
*/

/*
# Copyright (c) 2011 Travis Robinson <libusbdotnet@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
# 	  
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL TRAVIS LEE ROBINSON 
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
# THE POSSIBILITY OF SUCH DAMAGE. 
#
*/


#include "USBAudioDevice.h"

bool USBAudioDevice::FirmwareLoaded = false;

USBAudioDevice::USBAudioDevice(bool useInput) : m_fbInfo(), m_dac(NULL), m_adc(NULL), m_feedback(NULL), m_useInput(useInput),
	m_lastParsedInterface(NULL), m_lastParsedEndpoint(NULL), m_audioClass(0),
	m_dacEndpoint(NULL), m_adcEndpoint(NULL), m_fbEndpoint(NULL), m_notifyCallback(NULL), m_notifyCallbackContext(NULL), m_isStarted(FALSE)
{
	InitDescriptors();

	m_cmd2Pckt.Byte0 = 0x00;
	m_cmd2Pckt.Byte1 = 0x20;
	m_cmd2Pckt.Byte2 = 0x00;
	m_cmd2Pckt.Byte3 = 0x00;


	HMODULE h = GetModuleHandle( "asiouac2.dll" );

	if (NULL == h)
		h = GetModuleHandle( "WidgetTest.exe" );


	GetModuleFileName( h, FolderLocation, sizeof(FolderLocation) );

	for (int i = strlen( FolderLocation )-1; i > 0; i--)
	{
		if (FolderLocation[i] == '\\')
		{
			FolderLocation[i] = 0;
			break;
		}
	}

	strcat( FolderLocation, "\\" );
}

USBAudioDevice::~USBAudioDevice()
{
	if(m_dac)
		delete m_dac;
	if(m_feedback)
		delete m_feedback;
	if(m_adc)
		delete m_adc;
}

void USBAudioDevice::InitDescriptors()
{
	//memset(&m_iad, 0, sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR));
	m_lastParsedInterface = NULL;
}

void USBAudioDevice::FreeDevice()
{
	FreeDeviceInternal();
	USBDevice::FreeDevice();
}

void USBAudioDevice::FreeDeviceInternal()
{
	InitDescriptors();
	if(m_dac)
		delete m_dac;
	if(m_feedback)
		delete m_feedback;
	if(m_adc)
		delete m_adc;

	m_feedback = NULL;
	m_adc = NULL;
	m_dac = NULL;
}

bool USBAudioDevice::ParseDescriptorInternal(USB_DESCRIPTOR_HEADER* uDescriptor)
{
	USB_INTERFACE_DESCRIPTOR* interfaceDescriptor;
	switch(uDescriptor->bDescriptorType)
	{
/*
		case USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION:
#ifdef _ENABLE_TRACE
			debugPrintf("ASIOUAC: Found IAD descriptor\n");
#endif
			memcpy(&m_iad, uDescriptor, sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR));
		return TRUE;
*/
		case USB_DESCRIPTOR_TYPE_INTERFACE:
		{
			interfaceDescriptor = (USB_INTERFACE_DESCRIPTOR*)uDescriptor;
			switch(interfaceDescriptor->bInterfaceSubClass)
			{
				case AUDIO_INTERFACE_SUBCLASS_AUDIOCONTROL:
					{	
#ifdef _ENABLE_TRACE
						debugPrintf("ASIOUAC: Found audio control interface 0x%X\n", interfaceDescriptor->bInterfaceNumber);
#endif
						USBAudioControlInterface *iACface = new USBAudioControlInterface(interfaceDescriptor);
						m_lastParsedInterface = iACface;
						m_acInterfaceList.Add(iACface);
					}
					return TRUE;

				case AUDIO_INTERFACE_SUBCLASS_AUDIOSTREAMING:
					{
#ifdef _ENABLE_TRACE
						debugPrintf("ASIOUAC: Found audio streaming interface 0x%X (alt num 0x%X) with %d endpoints\n", interfaceDescriptor->bInterfaceNumber, 
							interfaceDescriptor->bAlternateSetting, interfaceDescriptor->bNumEndpoints);
#endif
						USBAudioStreamingInterface *iASface = new USBAudioStreamingInterface(interfaceDescriptor);
						m_lastParsedInterface = iASface;
						m_asInterfaceList.Add(iASface);
					}					
					return TRUE;

				case USB_DEVICE_CLASS_VENDOR_SPECIFIC:
					{
#ifdef _ENABLE_TRACE
						debugPrintf("ASIOUAC: Found audio streaming interface 0x%X (alt num 0x%X) with %d endpoints\n", interfaceDescriptor->bInterfaceNumber, 
							interfaceDescriptor->bAlternateSetting, interfaceDescriptor->bNumEndpoints);
#endif
						USBFirmwareInterface *iACface = new USBFirmwareInterface(interfaceDescriptor);
						m_lastParsedInterface = iACface;
						m_fwInterfaceList.Add(iACface);
					}
					return TRUE;


				// BCA interface descriptor is type 0
				case 00:
					{
#ifdef _ENABLE_TRACE
						debugPrintf("ASIOUAC: Found BCA with firmware interface 0x%X (alt num 0x%X) with %d endpoints\n", interfaceDescriptor->bInterfaceNumber, 
							interfaceDescriptor->bAlternateSetting, interfaceDescriptor->bNumEndpoints);
#endif
						USBFirmwareInterface *iACface = new USBFirmwareInterface(interfaceDescriptor);
						m_lastParsedInterface = iACface;
						m_fwInterfaceList.Add(iACface);
					}
					return TRUE;


				default:
					m_lastParsedInterface = NULL;
					return FALSE;
			}
		}
		break;
		
		case CS_INTERFACE:
			if(m_lastParsedInterface)
			{
				bool retVal = m_lastParsedInterface->SetCSDescriptor(uDescriptor);
				if(m_audioClass == 0 && m_lastParsedInterface->Descriptor().bInterfaceSubClass == AUDIO_INTERFACE_SUBCLASS_AUDIOCONTROL)
					m_audioClass = ((USBAudioControlInterface*)m_lastParsedInterface)->m_acDescriptor.bcdADC == 0x200 ? 2 : 1;
				return retVal;
			}
		return FALSE;

		case USB_DESCRIPTOR_TYPE_ENDPOINT:
			if(m_lastParsedInterface)
			{
				m_lastParsedEndpoint = m_lastParsedInterface->CreateEndpoint((USB_ENDPOINT_DESCRIPTOR *)uDescriptor);
				return m_lastParsedEndpoint != NULL;
			}
		return FALSE;

		case CS_ENDPOINT:
			if(m_lastParsedEndpoint)
				return m_lastParsedEndpoint->SetCSDescriptor(uDescriptor);
		return FALSE;
	}

	return FALSE;
}

bool USBAudioDevice::InitDevice()
{
	SuppressDebug = false;

	InitMutex();

	if(!USBDevice::InitDevice())
		return FALSE;

	// if detected the boot device, then load firmware
	// and re-initialise.
	if (IsBoot)
	{
		LoadBootCode( );
		Sleep(5000);
		if(!USBDevice::InitDevice())
			return FALSE;
	}

	if (IsBoot)
	{
		return FALSE;
	}

	if (!LoadFPGACode( ))
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Failed to load FPGA code\n" );
#endif
		return FALSE;
	}



	if(m_useInput)
	{
		USBFirmwareEndpoint *epoint = FindFWEndpoint( 0x86 ) ;

		if (epoint)
		{
			m_adc = new AudioADC();
			m_adc->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, 
				epoint->m_descriptor.wMaxPacketSize, 
				epoint->m_descriptor.bInterval, 
				8, 
				4);
			m_adc->SetSampleFreq( 48000 );
			m_adcEndpoint = epoint;
		}

	}

	{
		USBFirmwareEndpoint *epoint = FindFWEndpoint( 0x02 ) ;
		if (epoint)
		{
			m_dac = new AudioDAC();
			m_dac->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, epoint->m_descriptor.wMaxPacketSize, 
				epoint->m_descriptor.bInterval, 
				8, 
				4);
			m_dac->SetSampleFreq( 48000 );
			m_dacEndpoint = epoint;
		}
	}


	// create feedback reader
	{
		USBFirmwareEndpoint *epoint = FindFWEndpoint( 0x81 ) ;
		if (epoint)
		{
			m_feedback = new AudioFeedback();
			m_feedback->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, epoint->m_descriptor.wMaxPacketSize, 
				epoint->m_descriptor.bInterval, 
				64,
				m_dac);
			m_fbEndpoint = epoint;
		}
	}


#ifdef NOTHERENOW

		USBAudioStreamingInterface * iface = m_asInterfaceList.First();
		while(iface)
		{
			USBAudioStreamingEndpoint * epoint = iface->m_endpointsList.First();


			while(epoint)
			{
				if(USB_ENDPOINT_DIRECTION_IN(epoint->m_descriptor.bEndpointAddress) && 
					(epoint->m_descriptor.bmAttributes & 0x03) == USB_ENDPOINT_TYPE_ISOCHRONOUS &&
					(epoint->m_descriptor.bmAttributes & 0x0C) != 0) //not feedback
				{
#ifdef _ENABLE_TRACE
				    debugPrintf("ASIOUAC: Found input endpoint 0x%X\n",  (int)epoint->m_descriptor.bEndpointAddress);
#endif
					int channelNumber = 2;
					USBAudioOutTerminal* outTerm = FindOutTerminal(iface->m_asgDescriptor.bTerminalLink);
					if(outTerm)
					{
						USBAudioFeatureUnit* unit = FindFeatureUnit(outTerm->m_outTerminal.bSourceID);
						if(unit)
						{
							USBAudioInTerminal* inTerm = FindInTerminal(unit->m_featureUnit.bSourceID);
							if(inTerm)
								channelNumber = inTerm->m_inTerminal.bNrChannels;
						}
					}

					m_adc = new AudioADC();
					m_adc->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, 
						epoint->m_descriptor.wMaxPacketSize, 
						epoint->m_descriptor.bInterval, 
						channelNumber, 
						iface->m_formatDescriptor.bSubslotSize);
					m_adcEndpoint = epoint;
					break;
				}
				epoint = iface->m_endpointsList.Next(epoint);
			}
			if(m_adc != NULL)
				break;
			iface = m_asInterfaceList.Next(iface);
		}
		if(m_adc == NULL)
			m_useInput = FALSE;
	}

	if(m_adc == NULL)
	{
		USBAudioStreamingInterface * iface = m_asInterfaceList.First();
		while(iface)
		{
			USBAudioStreamingEndpoint * epoint = iface->m_endpointsList.First();
			while(epoint)
			{
				if(USB_ENDPOINT_DIRECTION_IN(epoint->m_descriptor.bEndpointAddress) && 
					(epoint->m_descriptor.bmAttributes & 0x03) == USB_ENDPOINT_TYPE_ISOCHRONOUS &&
					(epoint->m_descriptor.bmAttributes & 0x0C) == 0) //feedback
				{
#ifdef _ENABLE_TRACE
					debugPrintf("ASIOUAC: Found feedback endpoint 0x%X\n",  (int)epoint->m_descriptor.bEndpointAddress);
#endif
					m_feedback = new AudioFeedback();
					m_feedback->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, epoint->m_descriptor.wMaxPacketSize, epoint->m_descriptor.bInterval, 4);
					m_fbEndpoint = epoint;
					break;
				}
				epoint = iface->m_endpointsList.Next(epoint);
			}
			if(m_feedback != NULL)
				break;
			iface = m_asInterfaceList.Next(iface);
		}
	}
//	return TRUE;

	USBAudioStreamingInterface * iface = m_asInterfaceList.First();
	while(iface)
	{
		if(!m_fbEndpoint || m_fbEndpoint->m_interface == iface) //out endpoint and feedback endpoint in same interface
		{
			USBAudioStreamingEndpoint * epoint = iface->m_endpointsList.First();
			while(epoint)
			{
				if(USB_ENDPOINT_DIRECTION_OUT(epoint->m_descriptor.bEndpointAddress) && 
					(epoint->m_descriptor.bmAttributes & 0x03) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
				{
#ifdef _ENABLE_TRACE
					debugPrintf("ASIOUAC: Found output endpoint 0x%X\n",  (int)epoint->m_descriptor.bEndpointAddress);
#endif
					int channelNumber = 2;
					USBAudioInTerminal* inTerm = FindInTerminal(iface->m_asgDescriptor.bTerminalLink);
					if(inTerm)
						channelNumber = inTerm->m_inTerminal.bNrChannels;

					m_dac = new AudioDAC();
					m_dac->Init(this, &m_fbInfo, epoint->m_descriptor.bEndpointAddress, epoint->m_descriptor.wMaxPacketSize, 
						epoint->m_descriptor.bInterval, 
						channelNumber, 
						iface->m_formatDescriptor.bSubslotSize);
					m_dacEndpoint = epoint;
					break;
				}
				epoint = iface->m_endpointsList.Next(epoint);
			}
		}
		if(m_dac != NULL)
			break;
		iface = m_asInterfaceList.Next(iface);
	}

#endif

	return TRUE;
}


bool USBAudioDevice::LoadBootCode( )
{

	char FileLocation[_MAX_PATH];
	strcpy(FileLocation, FolderLocation);

	USBFirmwareEndpoint* FWEndpoint = FindFWDest();

	unsigned char  buf[512];

	ZeroMemory(buf,512);

	LONG bytesToSend = 1;
	bool retValue;
	UINT lengthTransferred = 0;

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: Found BCA-2000 in boot mode\n" );
#endif

	// halt CPU
	buf[0] = 1;
	retValue = SendUsbControl(BMREQUEST_DIR_HOST_TO_DEVICE, BMREQUEST_TYPE_VENDOR, BMREQUEST_RECIPIENT_DEVICE, 
			0xA0, 
			0xe600, 
			0,
			(unsigned char*)buf, 1, &lengthTransferred);

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: Sending firmware \"BCA2000-2106-firmwarebin.bin\"\n" );
#endif

	strcat( FileLocation, "BCA2000-2106-firmwarebin.bin" );

	FILE *f = fopen( FileLocation, "rb" );
	if (f)
	{
		bytesToSend = 512;
		for (int i = 0; i < 8192/bytesToSend; i++)
		{

			unsigned short Value = i * bytesToSend;
			int bytesRead = fread( buf, 1, bytesToSend, f );

			retValue = SendUsbControl(BMREQUEST_DIR_HOST_TO_DEVICE, BMREQUEST_TYPE_VENDOR, BMREQUEST_RECIPIENT_DEVICE, 
				0xA0, 
				Value, 
				0,
				(unsigned char*)buf, bytesToSend, &lengthTransferred);
		}
		fclose(f);
	}

	// start CPU
	buf[0] = 0;
	retValue = SendUsbControl(BMREQUEST_DIR_HOST_TO_DEVICE, BMREQUEST_TYPE_VENDOR, BMREQUEST_RECIPIENT_DEVICE, 
			0xA0, 
			0xe600, 
			0,
			(unsigned char*)buf, 1, &lengthTransferred);

	FreeDevice();

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: Will now re-enumerate\n" );
#endif


	return false;
}



bool USBAudioDevice::PostBCACmd( unsigned char Cmd, unsigned char Len, unsigned char *data, int datalen )
{
	USBFirmwareEndpoint* FWEndpoint = FindFWEndpoint( 0x01 );


	unsigned char  buf[512];

	ZeroMemory(buf,512);

	buf[0] = Cmd;
	buf[1] = Len;
	if (data)
	{
		if (datalen)
			memcpy( buf+2, data, datalen );
		else
			memcpy( buf+2, data, Len );
	}

	long len = 52;

	switch (Cmd)
	{
	case 7:
		len = 52;
		break;
	case 2:
		len = 64;
		break;
	}


#ifdef _ENABLE_TRACE
	if (!SuppressDebug)
	{
		char t[300];
		char *p = t;
		sprintf(p, "Sending Cmd 0x%02.2X len 0x%02.2X: ", Cmd, Len );
		p += strlen(p);
		for (int i = 0; i < 52; i++)
		{
			sprintf(p, "%02.2X ", buf[i] );
			p += strlen(p);
		}

		debugPrintf("ASIOUAC: %s\n", t );
	}
#endif


	UINT lengthTransferred = 0;
	BOOL res = UsbK_WritePipe	( 
		m_usbDeviceHandle,
		FWEndpoint->m_descriptor.bEndpointAddress,
		(unsigned char*)buf, 
		len, 
		&lengthTransferred,
		NULL );	

	return false;
}

bool USBAudioDevice::PostBCAControl02( unsigned char B0, unsigned char B1, unsigned char B2, unsigned char B3, unsigned char B4  )
{
	unsigned char t[5];
	t[0] = B0;
	t[1] = B1;
	t[2] = B2;
	t[3] = B3;

	// extra byte seems to be looked at by firmware
	t[4] = B4;

	return PostBCACmd( 02, 4, t, sizeof(t) );
}


bool USBAudioDevice::PostBCAControl12( unsigned char B0, unsigned char B1, unsigned char B2, unsigned char B3, unsigned char B4, unsigned char B5, unsigned char B6  )
{
	unsigned char t[7];
	t[0] = B0;
	t[1] = B1;
	t[2] = B2;
	t[3] = B3;
	t[4] = B4;
	t[5] = B5;
	t[6] = B6;

	return PostBCACmd( 12, sizeof(t), t );
}



bool USBAudioDevice::LoadFPGACode( )
{
	char FileLocation[_MAX_PATH];
	strcpy(FileLocation, FolderLocation);


	if (!FirmwareLoaded)
	{
		unsigned char  buf[512];

		ZeroMemory(buf,512);

		LONG bytesToSend = 1;
	#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Found BCA-2000 with firmware\n" );
		debugPrintf("ASIOUAC: Loading FPGA code \"BCA2000-2106-fpga.bin\"\n" );
	#endif

		strcat( FileLocation, "BCA2000-2106-fpga.bin" );

		FILE *f = fopen( FileLocation, "rb" );

		if (f)
		{
			SuppressDebug = true;
			int Total = 0;
			bytesToSend = 32;
			while (bytesToSend == 32)
			{
				bytesToSend = fread( buf, 1, bytesToSend, f );
				if (bytesToSend < 32)
				{
					PostBCACmd( 0x10, bytesToSend, buf );
				}
				else
				{
					PostBCACmd( 0x10, bytesToSend, buf );
				}

				Total += bytesToSend;
	#ifdef _ENABLE_TRACE
				if (!(Total % 2560))
					debugPrintf("  ASIOUAC: %06.6d bytes\r", Total );
	#endif
			}
			fclose(f);

			// tell it it's finished
			PostBCACmd( 0x11, 0, buf );

			SuppressDebug = false;

	#ifdef _ENABLE_TRACE
			debugPrintf("\nASIOUAC: FPGA Load complete\n", Total );
	#endif
			FirmwareLoaded = true;
		}
	}

	m_cmd2Pckt.Byte0 = 0;
	m_cmd2Pckt.Byte1 = 0x20;
	m_cmd2Pckt.Byte2 = 0x00;
	m_cmd2Pckt.Byte3 = 0x0;

	PostBCAControl02( m_cmd2Pckt.Byte0, m_cmd2Pckt.Byte1, m_cmd2Pckt.Byte2, m_cmd2Pckt.Byte3 );

	// set t0 pause data
	m_cmd2Pckt.Byte0 = 0xc0;
	m_cmd2Pckt.Byte1 = 0x38;
	PostBCAControl02( m_cmd2Pckt.Byte0, m_cmd2Pckt.Byte1, m_cmd2Pckt.Byte2, m_cmd2Pckt.Byte3 );

	//unsigned char tmp[20] = { 0, 0, 5, 0, 0, 0 };
	//PostBCACmd( 0x07, 0, tmp, 6 );

	if (FirmwareLoaded)
		return true;

	return false;
}


void USBAudioDevice::EnableOutput()
{
	m_cmd2Pckt.Byte0 |= 0x02; // 8 chan out? 
	PostBCAControl02( m_cmd2Pckt.Byte0, m_cmd2Pckt.Byte1, m_cmd2Pckt.Byte2, m_cmd2Pckt.Byte3 );
}

void USBAudioDevice::EnableRx()
{
	m_cmd2Pckt.Byte1 &= 0xDF;// enable Rx

	m_cmd2Pckt.Byte1 &= 0xF0;// don;t know exactly
	m_cmd2Pckt.Byte1 |= 0x0A; 

	PostBCAControl02( m_cmd2Pckt.Byte0, m_cmd2Pckt.Byte1, m_cmd2Pckt.Byte2, m_cmd2Pckt.Byte3 );

	unsigned char tmp[20] = { 0, 0, 5, 0, 0, 0 };
	PostBCACmd( 0x07, 0, tmp, 6 );

}


bool USBAudioDevice::CheckSampleRate(USBAudioClockSource* clocksrc, int newfreq)
{
	bool retVal = FALSE;

	switch (newfreq)
	{
	case 44100:
	case 48000:
//	case 88200:
	case 96000:
		retVal = TRUE;
		break;
	default:
		break;
	}

#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Sample freq: %d %s\n", newfreq, retVal ? "is supported" : "isn't supported");
#endif

	return retVal;
}

USBFirmwareEndpoint* USBAudioDevice::FindFWDest()
{
	USBFirmwareInterface * iface = m_fwInterfaceList.First();

	while(iface)
	{
		if (iface->m_endpointsList.Count())
		{
			USBFirmwareEndpoint *ep;
			ep = iface->m_endpointsList.First();
			while(ep)
			{
				if (ep->m_descriptor.bEndpointAddress == 1)
					return ep;
				ep = iface->m_endpointsList.Next(ep);
			}
		}
		iface = m_fwInterfaceList.Next(iface);
	}
	return NULL;
}




USBFirmwareEndpoint* USBAudioDevice::FindFWEndpoint( int Addr )
{
	USBFirmwareInterface * iface = m_fwInterfaceList.First();

	while(iface)
	{
		if (iface->m_endpointsList.Count())
		{
			USBFirmwareEndpoint *ep;
			ep = iface->m_endpointsList.First();
			while(ep)
			{
				if (ep->m_descriptor.bEndpointAddress == Addr)
					return ep;
				ep = iface->m_endpointsList.Next(ep);
			}
		}
		iface = m_fwInterfaceList.Next(iface);
	}
	return NULL;
}





USBAudioClockSource* USBAudioDevice::FindClockSource(int freq)
{
	USBAudioControlInterface * iface = m_acInterfaceList.First();
	while(iface)
	{
		USBAudioClockSource* clockSource = iface->m_clockSourceList.First();
		while(clockSource)
		{
			if(CheckSampleRate(clockSource, freq))
				return clockSource;
			clockSource = iface->m_clockSourceList.Next(clockSource);
		}
		
		iface = m_acInterfaceList.Next(iface);
	}
	return NULL;
}

bool USBAudioDevice::SetSampleRateInternal(int freq)
{
	bool retVal = FALSE;

	retVal = TRUE;
	switch (freq)
	{
	case 44100:
		m_cmd2Pckt.Byte0 &= 0xFB;
		m_cmd2Pckt.Byte0 |= 0x00;
		m_cmd2Pckt.Byte2 &= 0xCF;
		m_cmd2Pckt.Byte2 |= 0x00;
		break;

	case 48000:
		m_cmd2Pckt.Byte0 &= 0xFB;
		m_cmd2Pckt.Byte0 |= 0x04;
		m_cmd2Pckt.Byte2 &= 0xCF;
		m_cmd2Pckt.Byte2 |= 0x10;
		break;

	case 88200:
		m_cmd2Pckt.Byte0 &= 0xFB;
		m_cmd2Pckt.Byte0 |= 0x0C;
		m_cmd2Pckt.Byte2 &= 0xCF;
		m_cmd2Pckt.Byte2 |= 0x20;
		break;

	case 96000:
		m_cmd2Pckt.Byte0 &= 0xFB;
		m_cmd2Pckt.Byte0 |= 0x0C;
		m_cmd2Pckt.Byte2 &= 0xCF;
		m_cmd2Pckt.Byte2 |= 0x30;
		break;

	default:
		retVal = FALSE;
		break;
	}

	if (m_adc)
		m_adc->SetSampleFreq( freq );

	if (m_dac)
		m_dac->SetSampleFreq( freq );

	PostBCAControl02( m_cmd2Pckt.Byte0, m_cmd2Pckt.Byte1, m_cmd2Pckt.Byte2, m_cmd2Pckt.Byte3 );

#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Sample freq: %d %s\n", freq, retVal ? "is supported" : "isn't supported");
#endif

	return retVal;
}

int USBAudioDevice::GetCurrentSampleRate()
{
	int Res = 48000;

	if(!IsValidDevice())
		return 0;

	switch (m_cmd2Pckt.Byte2 & 0x30)
	{
	case 0x00:
		Res = 44100;
		break;
	case 0x01:
		Res = 48000;
		break;
	case 0x02:
		Res = 88200;
		break;
	case 0x03:
		Res = 96000;
		break;
	default:
		break;
	}

	return Res;
}

int USBAudioDevice::GetSampleRateInternal(int interfaceNum, int clockID)
{
	return 48000;
}

bool USBAudioDevice::SetSampleRate(int freq)
{
	if(!IsValidDevice())
		return FALSE;

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: Set samplerate %d\n",  freq);
#endif
	if(SetSampleRateInternal(freq))
	{
		return TRUE;
	}
	return FALSE;
}

bool USBAudioDevice::CanSampleRate(int freq)
{
	if(!IsValidDevice())
		return FALSE;

	return(CheckSampleRate(NULL, freq));
}


bool USBAudioDevice::Start()
{
	if(m_isStarted || !IsValidDevice())
		return FALSE;

	bool retVal = TRUE;

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: USBAudioDevice start\n");
#endif


	// NOTE:
	// MUST be reading status, else it won't start up tx!!!!
	if (m_fbEndpoint)
	{
		if(m_feedback != NULL)
			retVal &= m_feedback->Start();
	}

	Sleep(500);

	if(m_adcEndpoint)
	{
		UsbClaimInterface(m_adcEndpoint->m_interface->Descriptor().bInterfaceNumber);
		UsbSetAltInterface(m_adcEndpoint->m_interface->Descriptor().bInterfaceNumber, m_adcEndpoint->m_interface->Descriptor().bAlternateSetting);
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Claim ADC interface 0x%X (alt 0x%X)\n", m_adcEndpoint->m_interface->Descriptor().bInterfaceNumber, 
			m_adcEndpoint->m_interface->Descriptor().bAlternateSetting);
#endif
		if(m_adc != NULL)
			retVal &= m_adc->Start();
	}


	if(m_dacEndpoint)
	{
		UsbClaimInterface(m_dacEndpoint->m_interface->Descriptor().bInterfaceNumber);
		UsbSetAltInterface(m_dacEndpoint->m_interface->Descriptor().bInterfaceNumber, m_dacEndpoint->m_interface->Descriptor().bAlternateSetting);
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Claim DAC interface 0x%X (alt 0x%X)\n", m_dacEndpoint->m_interface->Descriptor().bInterfaceNumber, 
			m_dacEndpoint->m_interface->Descriptor().bAlternateSetting);
#endif
		if(m_dac != NULL)
			retVal &= m_dac->Start();

	}


//	unsigned char tmp[20] = { 0, 0, 5, 0, 0, 0 };
//	PostBCACmd( 0x07, 0, tmp, 6 );


	m_isStarted = TRUE;
	return retVal;
}

bool USBAudioDevice::Stop()
{
	if(!m_isStarted || !IsValidDevice())
		return FALSE;

#ifdef _ENABLE_TRACE
	debugPrintf("ASIOUAC: USBAudioDevice stop\n");
#endif
	bool retVal = TRUE;

	if(m_dac != NULL)
		retVal &= m_dac->Stop();

	if(m_feedback != NULL)
		retVal &= m_feedback->Stop();

	if(m_adc != NULL)
		retVal &= m_adc->Stop();

	if(!IsConnected())
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Device isn't connected\n");
#endif
		m_isStarted = FALSE;
		return FALSE;
	}

	if(m_adcEndpoint)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Try release ADC interfaces\n");
#endif
		USBAudioStreamingInterface * iface = m_asInterfaceList.First();
		while(iface)
		{
			if(iface->Descriptor().bInterfaceNumber == m_adcEndpoint->m_interface->Descriptor().bInterfaceNumber &&
				iface->m_endpointsList.Count() == 0)
			{
			
				UsbSetAltInterface(iface->Descriptor().bInterfaceNumber, iface->Descriptor().bAlternateSetting);
				UsbReleaseInterface(iface->Descriptor().bInterfaceNumber);
#ifdef _ENABLE_TRACE
				debugPrintf("ASIOUAC: Release ADC interface 0x%X\n", iface->Descriptor().bInterfaceNumber);
#endif
				break;
			}
			iface = m_asInterfaceList.Next(iface);
		}
	}
	if(m_dacEndpoint)
	{
#ifdef _ENABLE_TRACE
		debugPrintf("ASIOUAC: Try release DAC interfaces\n");
#endif
		USBAudioStreamingInterface * iface = m_asInterfaceList.First();
		while(iface)
		{
			if(iface->Descriptor().bInterfaceNumber == m_dacEndpoint->m_interface->Descriptor().bInterfaceNumber &&
				iface->m_endpointsList.Count() == 0)
			{
			
				UsbSetAltInterface(iface->Descriptor().bInterfaceNumber, iface->Descriptor().bAlternateSetting);
				UsbReleaseInterface(iface->Descriptor().bInterfaceNumber);
#ifdef _ENABLE_TRACE
				debugPrintf("ASIOUAC: Release DAC interface 0x%X\n", iface->Descriptor().bInterfaceNumber);
#endif
				break;
			}
			iface = m_asInterfaceList.Next(iface);
		}
	}

	m_isStarted = FALSE;
	return retVal;
}

void USBAudioDevice::SetDACCallback(FillDataCallback readDataCb, void* context)
{
	if(m_dac != NULL)
		m_dac->SetCallback(readDataCb, context);
}

void USBAudioDevice::SetADCCallback(FillDataCallback writeDataCb, void* context)
{
	if(m_adc != NULL)
		m_adc->SetCallback(writeDataCb, context);
}

int USBAudioDevice::GetInputChannelNumber()
{
	if(!IsValidDevice())
		return 0;
	return m_useInput ? 8 : 0;
}

int USBAudioDevice::GetOutputChannelNumber()
{
	if(!IsValidDevice())
		return 0;
	return 8;
}

USBAudioInTerminal* USBAudioDevice::FindInTerminal(int id)
{
	USBAudioControlInterface * iface = m_acInterfaceList.First();
	while(iface)
	{
		USBAudioInTerminal * elem = iface->m_inTerminalList.First();
		while(elem)
		{
			if(elem->m_inTerminal.bTerminalID == id)
				return elem;
			elem = iface->m_inTerminalList.Next(elem);
		}
		iface = m_acInterfaceList.Next(iface);
	}
	return NULL;
}

USBAudioFeatureUnit* USBAudioDevice::FindFeatureUnit(int id)
{
	USBAudioControlInterface * iface = m_acInterfaceList.First();
	while(iface)
	{
		USBAudioFeatureUnit * elem = iface->m_featureUnitList.First();
		while(elem)
		{
			if(elem->m_featureUnit.bUnitID == id)
				return elem;
			elem = iface->m_featureUnitList.Next(elem);
		}
		iface = m_acInterfaceList.Next(iface);
	}
	return NULL;
}

USBAudioOutTerminal* USBAudioDevice::FindOutTerminal(int id)
{
	USBAudioControlInterface * iface = m_acInterfaceList.First();
	while(iface)
	{
		USBAudioOutTerminal * elem = iface->m_outTerminalList.First();
		while(elem)
		{
			if(elem->m_outTerminal.bTerminalID == id)
				return elem;
			elem = iface->m_outTerminalList.Next(elem);
		}
		iface = m_acInterfaceList.Next(iface);
	}
	return NULL;
}
