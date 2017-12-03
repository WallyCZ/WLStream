#include "out_wavefile.h"

COutWaveFile::COutWaveFile(HMMIO *phFile) :
	m_hFile(phFile)
{
}

COutWaveFile::~COutWaveFile()
{
}

HRESULT COutWaveFile::OpenFile(LPCWSTR szFileName, IOutputPtr & output)
{
	MMIOINFO mi = { 0 };

	HMMIO* phFile = NULL;
	*phFile = mmioOpen(
		// some flags cause mmioOpen write to this buffer
		// but not any that we're using
		const_cast<LPWSTR>(szFileName),
		&mi,
		MMIO_READWRITE | MMIO_CREATE
	);

	if (NULL == *phFile) {
		ERR(L"mmioOpen(\"%ls\", ...) failed. wErrorRet == %u", szFileName, mi.wErrorRet);
		return E_FAIL;
	}

	
	output.reset(new COutWaveFile(phFile));

	return S_OK;
}

HRESULT COutWaveFile::Init(WAVEFORMATEX *pwfx) {

	//HRESULT WriteWaveHeader(HMMIO hFile, LPCWAVEFORMATEX pwfx, MMCKINFO *pm_ckRIFF, MMCKINFO *pm_ckData)
	MMRESULT result;

	// make a RIFF/WAVE chunk
	m_ckRIFF.ckid = MAKEFOURCC('R', 'I', 'F', 'F');
	m_ckRIFF.fccType = MAKEFOURCC('W', 'A', 'V', 'E');

	result = mmioCreateChunk(*m_hFile, &m_ckRIFF, MMIO_CREATERIFF);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioCreateChunk(\"RIFF/WAVE\") failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// make a 'fmt ' chunk (within the RIFF/WAVE chunk)
	MMCKINFO chunk;
	chunk.ckid = MAKEFOURCC('f', 'm', 't', ' ');
	result = mmioCreateChunk(*m_hFile, &chunk, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioCreateChunk(\"fmt \") failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// write the WAVEFORMATEX data to it
	LONG lBytesInWfx = sizeof(WAVEFORMATEX) + pwfx->cbSize;
	LONG lBytesWritten =
		mmioWrite(
			*m_hFile,
			reinterpret_cast<PCHAR>(const_cast<LPWAVEFORMATEX>(pwfx)),
			lBytesInWfx
		);
	if (lBytesWritten != lBytesInWfx) {
		ERR(L"mmioWrite(fmt data) wrote %u bytes; expected %u bytes", lBytesWritten, lBytesInWfx);
		return E_FAIL;
	}

	// ascend from the 'fmt ' chunk
	result = mmioAscend(*m_hFile, &chunk, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"fmt \" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// make a 'fact' chunk whose data is (DWORD)0
	chunk.ckid = MAKEFOURCC('f', 'a', 'c', 't');
	result = mmioCreateChunk(*m_hFile, &chunk, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioCreateChunk(\"fmt \") failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// write (DWORD)0 to it
	// this is cleaned up later
	DWORD frames = 0;
	lBytesWritten = mmioWrite(*m_hFile, reinterpret_cast<PCHAR>(&frames), sizeof(frames));
	if (lBytesWritten != sizeof(frames)) {
		ERR(L"mmioWrite(fact data) wrote %u bytes; expected %u bytes", lBytesWritten, (UINT32)sizeof(frames));
		return E_FAIL;
	}

	// ascend from the 'fact' chunk
	result = mmioAscend(*m_hFile, &chunk, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"fact\" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// make a 'data' chunk and leave the data pointer there
	m_ckData.ckid = MAKEFOURCC('d', 'a', 't', 'a');
	result = mmioCreateChunk(*m_hFile, &m_ckData, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioCreateChunk(\"data\") failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	return S_OK;
}

HRESULT COutWaveFile::ProcessBuffer(BYTE * buffer, size_t length, UINT32 nFrames) {

	LONG lBytesWritten = mmioWrite(*m_hFile, reinterpret_cast<PCHAR>(buffer), length);

	if (length != (size_t) lBytesWritten) {
		ERR(L"mmioWrite wrote %u bytes on pass %u after %u frames: expected %u bytes", lBytesWritten, -1, nFrames, length);
		getchar();
		return E_UNEXPECTED;
	}

	return S_OK;

}

int COutWaveFile::DeInit(UINT32 nFrames) {

	//from FinishWaveFile
	MMRESULT result;

	result = mmioAscend(*m_hFile, &m_ckData, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"data\" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	result = mmioAscend(*m_hFile, &m_ckRIFF, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"RIFF/WAVE\" failed: MMRESULT = 0x%08x", result);
		return E_FAIL;
	}

	// everything went well... fixup the fact chunk in the file
	/*result = mmioClose(*m_hFile, 0);
	m_hFile = NULL;
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioClose failed: MMSYSERR = %u", result);
		return -__LINE__;
	}

	// reopen the file in read/write mode
	MMIOINFO mi = { 0 };
	m_hFile = mmioOpen(const_cast<LPWSTR>(prefs.m_szFilename), &mi, MMIO_READWRITE);
	if (NULL == prefs.m_hFile) {
		ERR(L"mmioOpen(\"%ls\", ...) failed. wErrorRet == %u", prefs.m_szFilename, mi.wErrorRet);
		return -__LINE__;
	}*/

	result = mmioSeek(*m_hFile, 0, SEEK_SET);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioSeek(\"WAVE\") failed: MMSYSERR = %u", result);
		return -__LINE__;
	}



	// descend into the RIFF/WAVE chunk
	//MMCKINFO m_ckRIFF = { 0 };
	m_ckRIFF.ckid = MAKEFOURCC('W', 'A', 'V', 'E'); // this is right for mmioDescend
	result = mmioDescend(*m_hFile, &m_ckRIFF, NULL, MMIO_FINDRIFF);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioDescend(\"WAVE\") failed: MMSYSERR = %u", result);
		return -__LINE__;
	}

	// descend into the fact chunk
	MMCKINFO ckFact = { 0 };
	ckFact.ckid = MAKEFOURCC('f', 'a', 'c', 't');
	result = mmioDescend(*m_hFile, &ckFact, &m_ckRIFF, MMIO_FINDCHUNK);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioDescend(\"fact\") failed: MMSYSERR = %u", result);
		return -__LINE__;
	}

	// write the correct data to the fact chunk
	LONG lBytesWritten = mmioWrite(
		*m_hFile,
		reinterpret_cast<PCHAR>(&nFrames),
		sizeof(nFrames)
	);
	if (lBytesWritten != sizeof(nFrames)) {
		ERR(L"Updating the fact chunk wrote %u bytes; expected %u", lBytesWritten, (UINT32)sizeof(nFrames));
		return -__LINE__;
	}

	// ascend out of the fact chunk
	result = mmioAscend(*m_hFile, &ckFact, 0);
	if (MMSYSERR_NOERROR != result) {
		ERR(L"mmioAscend(\"fact\") failed: MMSYSERR = %u", result);
		return -__LINE__;
	}


	return 0;


}
