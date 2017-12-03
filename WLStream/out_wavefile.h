#pragma once
#include "common.h"
#include "out_interface.h"
#include <memory>

class COutWaveFile : public IOutput
{
	HMMIO *m_hFile;
	MMCKINFO m_ckRIFF = { 0 };
	MMCKINFO m_ckData = { 0 };

	COutWaveFile(HMMIO *phFile);

public:

	virtual ~COutWaveFile();

	static HRESULT OpenFile(LPCWSTR szFileName, IOutputPtr& output);

	virtual HRESULT Init(WAVEFORMATEX *pwfx);
	virtual HRESULT ProcessBuffer(BYTE* buffer, size_t length, UINT32 nFrames);
	virtual int DeInit(UINT32 nFrames);
};


typedef std::shared_ptr<COutWaveFile> COutWaveFilePtr;
