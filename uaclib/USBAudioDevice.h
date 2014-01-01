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


#pragma once
#ifndef __USBAUDIO_DEVICE_H__
#define __USBAUDIO_DEVICE_H__

#include "usbdevice.h"
#include "audiotask.h"
#include "tlist.h"
#include "descriptors.h"


typedef void (*NotifyCallback)(void* context, int reason);

typedef TList<USBAudioControlInterface> USBACInterfaceList;
typedef TList<USBAudioStreamingInterface> USBASInterfaceList;
typedef TList<USBFirmwareInterface> USBFWInterfaceList;


#pragma pack (push, 1)
typedef struct tag_Cmd2_Packet
{
	unsigned char Byte0;
	unsigned char Byte1;
	unsigned char Byte2;
	unsigned char Byte3;
} CMD2_PACKET;
#pragma pack (pop)



class USBAudioDevice : public USBDevice
{
	//IAD
	//USB_INTERFACE_ASSOCIATION_DESCRIPTOR	m_iad;

	USBACInterfaceList				m_acInterfaceList;
	USBASInterfaceList				m_asInterfaceList;
	USBFWInterfaceList				m_fwInterfaceList;

	static bool FirmwareLoaded;

	void FreeDeviceInternal();
	void InitDescriptors();

	int								m_audioClass;
	bool							m_useInput;

	char FolderLocation[_MAX_PATH];

	USBFirmwareEndpoint*		m_dacEndpoint;
	USBFirmwareEndpoint*		m_adcEndpoint;
	USBFirmwareEndpoint*		m_fbEndpoint;

	//USBAudioStreamingEndpoint*		m_dacEndpoint;
	//USBAudioStreamingEndpoint*		m_adcEndpoint;
	//USBAudioStreamingEndpoint*		m_fbEndpoint;

	NotifyCallback					m_notifyCallback;
	void*							m_notifyCallbackContext;

	CMD2_PACKET						m_cmd2Pckt;
protected:
	virtual void FreeDevice();


	virtual bool ParseDescriptorInternal(USB_DESCRIPTOR_HEADER* uDescriptor);

	bool SetSampleRateInternal(int freq);

	USBFirmwareEndpoint* FindFWDest();
	USBFirmwareEndpoint* FindFWEndpoint( int Addr );
	bool SuppressDebug;

	USBAudioClockSource* FindClockSource(int freq);
	bool CheckSampleRate(USBAudioClockSource* clocksrc, int freq);
	int GetSampleRateInternal(int interfaceNum, int clockID);

	USBAudioInTerminal*			FindInTerminal(int id);
	USBAudioFeatureUnit*		FindFeatureUnit(int id);
	USBAudioOutTerminal*		FindOutTerminal(int id);

	bool LoadBootCode( );
	bool LoadFPGACode( );
	bool PostBCACmd( unsigned char Cmd, unsigned char Len = 0, unsigned char *data = NULL, int datalen = 0);
	bool PostBCAControl02( unsigned char B0, unsigned char B1, unsigned char B2, unsigned char B3, unsigned char B4 = 0  );
	bool PostBCAControl12( unsigned char B0, unsigned char B1, unsigned char B2, unsigned char B3, unsigned char B4, unsigned char B5, unsigned char B6  );

public:
	USBAudioDevice(bool useInput);
	virtual ~USBAudioDevice();
	virtual bool InitDevice();

	bool CanSampleRate(int freq);
	bool SetSampleRate(int freq);
	int GetCurrentSampleRate();
	void EnableOutput();
	void EnableRx();

	int GetInputChannelNumber();
	int GetOutputChannelNumber();

	bool Start();
	bool Stop();

	void SetDACCallback(FillDataCallback readDataCb, void* context);
	void SetADCCallback(FillDataCallback writeDataCb, void* context);
	void SetNotifyCallback(NotifyCallback notifyCallback, void* notifyCallbackContext)
	{
		m_notifyCallback = notifyCallback;
		m_notifyCallbackContext = notifyCallbackContext;
	}

	int GetDACSubslotSize()
	{
		return 4;
	}
	int GetADCSubslotSize()
	{
		return 4;
	}
	int GetDACBitResolution()
	{
		return 32;
	}
	int GetADCBitResolution()
	{
		return 24;
	}
	int GetAudioClass()
	{
		return m_audioClass;
	}
	void Notify(int reason)
	{
		if(m_notifyCallback)
			m_notifyCallback(m_notifyCallbackContext, reason);
	}
private:
	USBAudioInterface*	m_lastParsedInterface;
	USBEndpoint*		m_lastParsedEndpoint;

	FeedbackInfo		m_fbInfo;
	AudioDAC*			m_dac;
	AudioADC*			m_adc;
	AudioFeedback*		m_feedback;

	bool				m_isStarted;
};

#endif //__USBAUDIO_DEVICE_H__
