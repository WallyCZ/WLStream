#pragma once
#include "common.h"
#include "out_interface.h"
#include <memory>

class COutConsole : public IOutput
{
public:
	COutConsole();
	virtual ~COutConsole();

	static HRESULT CreateOutput(IOutputPtr & output);

	virtual HRESULT Init(WAVEFORMATEX *pwfx);
	virtual HRESULT ProcessBuffer(BYTE* buffer, size_t length, UINT32 nFrames);
	virtual int DeInit(UINT32 nFrames);
};


typedef std::shared_ptr<COutConsole> COutConsolePtr;
