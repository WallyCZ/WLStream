#include "common.h"

HRESULT WLStream(
	IMMDevice *pMMDevice,
	IOutputPtr pOutput,
	bool bInt16,
	HANDLE hStartedEvent,
	HANDLE hStopEvent,
	PUINT32 pnFrames
);

//HRESULT WriteWaveHeader(HMMIO hFile, LPCWAVEFORMATEX pwfx, MMCKINFO *pckRIFF, MMCKINFO *pckData);
//HRESULT FinishWaveFile(HMMIO hFile, MMCKINFO *pckRIFF, MMCKINFO *pckData);

DWORD WINAPI WLStreamThreadFunction(LPVOID pContext) {
	WLStreamThreadFunctionArguments *pArgs =
		(WLStreamThreadFunctionArguments*)pContext;

	pArgs->hr = CoInitialize(NULL);
	if (FAILED(pArgs->hr)) {
		ERR(L"CoInitialize failed: hr = 0x%08x", pArgs->hr);
		return 0;
	}
	CoUninitializeOnExit cuoe;

	pArgs->hr = WLStream(
		pArgs->pMMDevice,
		pArgs->pOutput,
		pArgs->bInt16,
		pArgs->hStartedEvent,
		pArgs->hStopEvent,
		&pArgs->nFrames
	);

	return 0;
}


HRESULT WLStream(
	IMMDevice *pMMDevice,
	IOutputPtr pOutput,
	bool bInt16,
	HANDLE hStartedEvent,
	HANDLE hStopEvent,
	PUINT32 pnFrames
) {
	HRESULT hr;

	// activate an IAudioClient
	IAudioClient *pAudioClient;
	hr = pMMDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&pAudioClient
	);
	if (FAILED(hr)) {
		ERR(L"IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseAudioClient(pAudioClient);

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetDevicePeriod failed: hr = 0x%08x", hr);
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetMixFormat failed: hr = 0x%08x", hr);
		return hr;
	}
	CoTaskMemFreeOnExit freeMixFormat(pwfx);

	if (bInt16) {
		// coerce int-16 wave format
		// can do this in-place since we're not changing the size of the format
		// also, the engine will auto-convert from float to int for us
		switch (pwfx->wFormatTag) {
		case WAVE_FORMAT_IEEE_FLOAT:
			pwfx->wFormatTag = WAVE_FORMAT_PCM;
			pwfx->wBitsPerSample = 16;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			break;

		case WAVE_FORMAT_EXTENSIBLE:
		{
			// naked scope for case-local variable
			PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
			if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
				pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
				pEx->Samples.wValidBitsPerSample = 16;
				pwfx->wBitsPerSample = 16;
				pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
				pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			}
			else {
				ERR(L"%s", L"Don't know how to coerce mix format to int-16");
				return E_UNEXPECTED;
			}
		}
		break;

		default:
			ERR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
			return E_UNEXPECTED;
		}
	}

	hr = pOutput->Init(pwfx);

	if (FAILED(hr)) {
		// WriteWaveHeader does its own logging
		return hr;
	}

	// create a periodic waitable timer
	HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (NULL == hWakeUp) {
		DWORD dwErr = GetLastError();
		ERR(L"CreateWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CloseHandleOnExit closeWakeUp(hWakeUp);

	UINT32 nBlockAlign = pwfx->nBlockAlign;
	*pnFrames = 0;

	// call IAudioClient::Initialize
	// note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	// do not work together...
	// the "data ready" event never gets set
	// so we're going to do a timer-driven loop
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		0, 0, pwfx, 0
	);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::Initialize failed: hr = 0x%08x", hr);
		return hr;
	}

	// activate an IAudioCaptureClient
	IAudioCaptureClient *pAudioCaptureClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pAudioCaptureClient
	);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetService(IAudioCaptureClient) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		ERR(L"AvSetMmThreadCharacteristics failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	AvRevertMmThreadCharacteristicsOnExit unregisterMmcss(hTask);

	// set the waitable timer
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
	LONG lTimeBetweenFires = (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds
	BOOL bOK = SetWaitableTimer(
		hWakeUp,
		&liFirstFire,
		lTimeBetweenFires,
		NULL, NULL, FALSE
	);
	if (!bOK) {
		DWORD dwErr = GetLastError();
		ERR(L"SetWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

	// call IAudioClient::Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		ERR(L"IAudioClient::Start failed: hr = 0x%08x", hr);
		return hr;
	}
	AudioClientStopOnExit stopAudioClient(pAudioClient);

	SetEvent(hStartedEvent);

	// loopback capture loop
	HANDLE waitArray[2] = { hStopEvent, hWakeUp };
	DWORD dwWaitResult;

	bool bDone = false;

	for (UINT32 nPasses = 0; !bDone; nPasses++) {
		// drain data while it is available
		UINT32 nNextPacketSize;
		for (
			hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
			SUCCEEDED(hr) && nNextPacketSize > 0;
			hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize)
			) {
			// get the captured data
			BYTE *pData;
			UINT32 nNumFramesToRead;
			DWORD dwFlags;

			hr = pAudioCaptureClient->GetBuffer(
				&pData,
				&nNumFramesToRead,
				&dwFlags,
				NULL,
				NULL
			);

			int lBytesToWrite = nNumFramesToRead * nBlockAlign;

			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
				getchar();
				return hr;
			}
			
			if (dwFlags != 0) {
				ZeroMemory(pData, lBytesToWrite);
			}
			
			if (0 == nNumFramesToRead) {
				ERR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames", nPasses, *pnFrames);
				getchar();
				return E_UNEXPECTED;
			}

#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")
			
			/*if (hFile != NULL)
			{
				int lBytesWritten = mmioWrite(hFile, reinterpret_cast<PCHAR>(pData), lBytesToWrite);

				if (lBytesToWrite != lBytesWritten) {
					ERR(L"mmioWrite wrote %u bytes on pass %u after %u frames: expected %u bytes", lBytesWritten, nPasses, *pnFrames, lBytesToWrite);
					getchar();
					return E_UNEXPECTED;
				}
			}*/

			pOutput->ProcessBuffer(pData, lBytesToWrite, *pnFrames);

			*pnFrames += nNumFramesToRead;

			hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
				getchar();
				return hr;
			}
		}

		if (FAILED(hr)) {
			ERR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
			getchar();
			return hr;
		}

		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			LOG(L"Received stop event after %u passes and %u frames", nPasses, *pnFrames);
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			ERR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames", dwWaitResult, nPasses, *pnFrames);
			getchar();
			return E_UNEXPECTED;
		}
	} // capture loop

	/*if (hFile != NULL)
		hr = FinishWaveFile(hFile, &ckData, &ckRIFF);*/

	pOutput->DeInit(*pnFrames);

	if (FAILED(hr)) {
		// FinishWaveFile does it's own logging
		return hr;
	}

	return hr;
}


/*HRESULT FinishWaveFile(HMMIO hFile, MMCKINFO *pckRIFF, MMCKINFO *pckData) {
	MMRESULT result;

	result = mmioAscend(hFile, pckData, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"data\" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	result = mmioAscend(hFile, pckRIFF, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"RIFF/WAVE\" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	return S_OK;
}*/