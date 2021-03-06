/*
*
* Copyright (C) 2016 OtherCrashOverride@users.noreply.github.com.
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2, as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
*/

#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>

#include "Codec.h"
#include "Element.h"
#include "InPin.h"






class AudioCodecElement : public Element
{
	const int alsa_channels = 2;

	InPinSPTR audioInPin;
	OutPinSPTR audioOutPin;
	AudioPinInfoSPTR outInfo;

	AVCodec* soundCodec = nullptr;
	AVCodecContext* soundCodecContext = nullptr;
	bool isFirstData = true;
	AudioFormatEnum audioFormat = AudioFormatEnum::Unknown;

	int streamChannels = 0;
	int outputChannels = 0;
	int sampleRate = 0;
	AVFrameBufferSPTR frame; // = std::make_shared<AVFrameBuffer>(shared_from_this());


	void SetupCodec()
	{
		switch (audioFormat)
		{				
		case AudioFormatEnum::Aac:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_AAC);
			break;

		case AudioFormatEnum::Ac3:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_AC3);
			break;

		case AudioFormatEnum::Dts:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_DTS);
			break;

		case AudioFormatEnum::Mpeg2Layer3:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_MP3);
			break;

		case AudioFormatEnum::WmaPro:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_WMAPRO);
			break;

		case AudioFormatEnum::DolbyTrueHD:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_TRUEHD);
			break;

		case AudioFormatEnum::EAc3:
			soundCodec = avcodec_find_decoder(AV_CODEC_ID_EAC3);
			break;

		//case AudioStreamType::Pcm:

		default:
			printf("Audio format %d is not supported.\n", (int)audioFormat);
			throw NotSupportedException();
		}

		if (!soundCodec)
		{
			throw Exception("codec not found\n");
		}


		soundCodecContext = avcodec_alloc_context3(soundCodec);
		if (!soundCodecContext)
		{
			throw Exception("avcodec_alloc_context3 failed.\n");
		}


		soundCodecContext->channels = alsa_channels;
		soundCodecContext->sample_rate = sampleRate;
		//soundCodecContext->sample_fmt = AV_SAMPLE_FMT_S16P; //AV_SAMPLE_FMT_FLTP; //AV_SAMPLE_FMT_S16P
		soundCodecContext->request_sample_fmt = AV_SAMPLE_FMT_FLTP; // AV_SAMPLE_FMT_S16P; //AV_SAMPLE_FMT_FLTP;
		soundCodecContext->request_channel_layout = AV_CH_LAYOUT_STEREO;

		/* open it */
		if (avcodec_open2(soundCodecContext, soundCodec, NULL) < 0)
		{
			throw Exception("could not open codec\n");
		}
	}


	void ProcessBuffer(AVPacketBufferSPTR buffer, AVFrameBufferSPTR frame)
	{
		AVPacket* pkt = buffer->GetAVPacket();
		AVFrame* decoded_frame = frame->GetAVFrame();


		// Decode audio
		//printf("Decoding frame (AVPacket=%p, size=%d).\n",
		//	buffer->GetAVPacket(), buffer->GetAVPacket()->size);

		int bytesDecoded = 0;
		while (IsExecuting() && bytesDecoded < pkt->size)
		{
			int got_frame = 0;
			int len = avcodec_decode_audio4(soundCodecContext,
				decoded_frame,
				&got_frame,
				pkt);

			//printf("avcodec_decode_audio4 len=%d\n", len);

			if (len < 0)
			{
				// Report the error, but otherwise ignore it.				
				char errmsg[1024] = { 0 };
				av_strerror(len, errmsg, 1024);

				Log("Error while decoding: %s\n", errmsg);

				break;
			}
			else
			{
				bytesDecoded += len;
			}

			Log("decoded audio frame OK (len=%x, pkt.size=%x)\n", len, buffer->GetAVPacket()->size);


			// Convert audio to ALSA format
			if (got_frame)
			{
				// Copy out the PCM data because libav fills the frame
				// with re-used data pointers.
				PcmFormat format;
				bool isInterleaved;
				switch (decoded_frame->format)
				{
				case AV_SAMPLE_FMT_S16:
					format = PcmFormat::Int16;
					isInterleaved = true;
					break;

				case AV_SAMPLE_FMT_S32:
					format = PcmFormat::Int32;
					isInterleaved = true;
					break;

				case AV_SAMPLE_FMT_FLT:
					format = PcmFormat::Float32;
					isInterleaved = true;
					break;

				case AV_SAMPLE_FMT_S16P:
					format = PcmFormat::Int16Planes;
					isInterleaved = false;
					break;

				case AV_SAMPLE_FMT_S32P:
					format = PcmFormat::Int32Planes;
					isInterleaved = false;
					break;

				case AV_SAMPLE_FMT_FLTP:
					format = PcmFormat::Float32Planes;
					isInterleaved = false;
					break;

				default:
					printf("Sample format (%d) not supported.\n", decoded_frame->format);
					throw NotSupportedException();
				}

#if 1
				PcmDataBufferSPTR pcmDataBuffer = std::make_shared<PcmDataBuffer>(
					shared_from_this(),
					format,
					decoded_frame->channels,
					decoded_frame->nb_samples);
				
				if (buffer->GetAVPacket()->pts != AV_NOPTS_VALUE)
				{
					//pcmDataBuffer->SetTimeStamp(av_q2d(buffer->TimeBase()) * buffer->GetAVPacket()->pts);
					
					pcmDataBuffer->SetTimeStamp(
						av_frame_get_best_effort_timestamp(frame->GetAVFrame()) *
						av_q2d(buffer->TimeBase()));
				}
				else
				{
					pcmDataBuffer->SetTimeStamp(-1);
				}

				
				//pcmDataBuffer->SetTimeStamp(buffer->TimeStamp());

				//printf("decodec audio frame pts=%f\n", pcmDataBuffer->TimeStamp());

				if (isInterleaved)
				{
					PcmData* pcmData = pcmDataBuffer->GetPcmData();
					memcpy(pcmData->Channel[0], decoded_frame->data[0], pcmData->ChannelSize);
				}
				else
				{
					int leftChannelIndex = av_get_channel_layout_channel_index(
						decoded_frame->channel_layout,
						AV_CH_FRONT_LEFT);

					int rightChannelIndex = av_get_channel_layout_channel_index(
						decoded_frame->channel_layout,
						AV_CH_FRONT_RIGHT);

					int centerChannelIndex = av_get_channel_layout_channel_index(
						decoded_frame->channel_layout,
						AV_CH_FRONT_CENTER);

					void* channels[3];
					channels[0] = (void*)decoded_frame->data[leftChannelIndex];
					channels[1] = (void*)decoded_frame->data[rightChannelIndex];

					if (decoded_frame->channels > 2)
					{
						channels[2] = (void*)decoded_frame->data[centerChannelIndex];
					}
					else
					{
						channels[2] = nullptr;
					}

					for (int i = 0; i < decoded_frame->channels; ++i)
					{
						PcmData* pcmData = pcmDataBuffer->GetPcmData();
						memcpy(pcmData->Channel[i], channels[i], pcmData->ChannelSize);
					}
				}

				audioOutPin->SendBuffer(pcmDataBuffer);
#else
				audioOutPin->SendBuffer(frame);
#endif
			}
		}
	}


public:

	virtual void Initialize() override
	{
		ClearOutputPins();
		ClearInputPins();

		// TODO: Pin format negotiation

		{
			// Create an audio in pin
			AudioPinInfoSPTR info = std::make_shared<AudioPinInfo>();
			info->Format = AudioFormatEnum::Unknown;
			info->Channels = 0;
			info->SampleRate = 0;

			ElementWPTR weakPtr = shared_from_this();
			audioInPin = std::make_shared<InPin>(weakPtr, info);
			AddInputPin(audioInPin);
		}

		{
			// Create an audio out pin
			outInfo = std::make_shared<AudioPinInfo>();
			outInfo->Format = AudioFormatEnum::Pcm;
			outInfo->Channels = 0;
			outInfo->SampleRate = 0;

			ElementWPTR weakPtr = shared_from_this();
			audioOutPin = std::make_shared<OutPin>(weakPtr, outInfo);
			AddOutputPin(audioOutPin);
		}


		// Create buffer(s)
		/*AVFrameBufferSPTR frame = std::make_shared<AVFrameBuffer>(shared_from_this());*/
		//audioOutPin->AcceptProcessedBuffer(frame);
		
		frame = std::make_shared<AVFrameBuffer>(shared_from_this());
	}

	virtual void DoWork() override
	{
		BufferSPTR buffer;
		BufferSPTR outBuffer;

		// Reap output buffers
		while (audioOutPin->TryGetAvailableBuffer(&buffer))
		{
			// New buffers are created as needed so just
			// drop this buffer.
			Wake();
		}


		//while (audioInPin->TryPeekFilledBuffer(&buffer) &&
		//	   audioOutPin->TryPeekAvailableBuffer(&outBuffer))
		if(audioInPin->TryGetFilledBuffer(&buffer))
		{
			// Both an input and output buffer are avaialable

			//// Get the inputs buffers
			//if (!audioInPin->TryGetFilledBuffer(&buffer))
			//{
			//	throw Exception("Internal consistency error.");
			//}

			if (buffer->Type() != BufferTypeEnum::AVPacket)
			{
				// The input buffer is not usuable for processing
				switch (buffer->Type())
				{
					case BufferTypeEnum::Marker:
					{
						MarkerBufferSPTR markerBuffer = std::static_pointer_cast<MarkerBuffer>(buffer);

						switch (markerBuffer->Marker())
						{
							case MarkerEnum::EndOfStream:
								// Send all Output Pins an EOS buffer					
								for (int i = 0; i < Outputs()->Count(); ++i)
								{
									MarkerBufferSPTR eosBuffer = std::make_shared<MarkerBuffer>(shared_from_this(), MarkerEnum::EndOfStream);
									Outputs()->Item(i)->SendBuffer(eosBuffer);
								}

								//SetExecutionState(ExecutionStateEnum::Idle);
								SetState(MediaState::Pause);
								break;

								//case MarkerEnum::Play:						
								//	SetState(MediaState::Play);
								//	break;

								//case MarkerEnum::Pause:
								//	SetState(MediaState::Pause);
								//	break;

							default:
								// ignore unknown 
								break;
						}
						break;
					}

					default:
						// Ignore
						break;
				}

				audioInPin->PushProcessedBuffer(buffer);
				audioInPin->ReturnProcessedBuffers();
			}
			else
			{
				//// Get the output buffer
				//if (!audioOutPin->TryGetAvailableBuffer(&outBuffer))
				//{
				//	throw Exception("Internal consistency error.");
				//}

				if (isFirstData)
				{
					OutPinSPTR otherPin = audioInPin->Source();
					if (otherPin)
					{
						if (otherPin->Info()->Category() != MediaCategoryEnum::Audio)
						{
							throw InvalidOperationException("AlsaAudioSinkElement: Not connected to an audio pin.");
						}

						AudioPinInfoSPTR info = std::static_pointer_cast<AudioPinInfo>(otherPin->Info());
						audioFormat = info->Format;
						sampleRate = info->SampleRate;
						streamChannels = info->Channels;

						outInfo->SampleRate = info->SampleRate;
						outInfo->Channels = info->Channels;

						printf("AudioCodecElement: outInfo->SampleRate=%d, outInfo->Channels=%d\n", outInfo->SampleRate, outInfo->Channels);

						SetupCodec();

						isFirstData = false;
					}
				}

				AVPacketBufferSPTR avPacketBuffer = std::static_pointer_cast<AVPacketBuffer>(buffer);
				//AVFrameBufferSPTR avFrameBuffer = std::static_pointer_cast<AVFrameBuffer>(outBuffer);
				
				//ProcessBuffer(avPacketBuffer, avFrameBuffer);
				ProcessBuffer(avPacketBuffer, frame);

				audioInPin->PushProcessedBuffer(buffer);
				audioInPin->ReturnProcessedBuffers();
			}
			
		}
		
	}

	virtual void ChangeState(MediaState oldState, MediaState newState) override
	{
		Element::ChangeState(oldState, newState);
	}

	virtual void Flush() override
	{
		Element::Flush();
		//WaitForExecutionState(ExecutionStateEnum::Idle);

		avcodec_flush_buffers(soundCodecContext);
	}
};


typedef std::shared_ptr<AudioCodecElement> AudioCodecElementSPTR;
