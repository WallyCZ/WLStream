#pragma once
#include <memory>

class IOutput
{
public:
	IOutput()
	{
	}
	virtual ~IOutput()
	{
	}

	virtual HRESULT Init(WAVEFORMATEX *pwfx) = 0;
	virtual HRESULT ProcessBuffer(BYTE* buffer, size_t length, UINT32 nFrames) = 0;
	virtual int DeInit(UINT32 nFrames) = 0;
};

typedef std::shared_ptr<IOutput> IOutputPtr;
