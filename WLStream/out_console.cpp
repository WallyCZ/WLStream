#include "out_console.h"

COutConsole::COutConsole()
{
}

COutConsole::~COutConsole()
{
}

HRESULT COutConsole::CreateOutput(IOutputPtr & output)
{
	output.reset(new COutConsole());

	return S_OK;
}


HRESULT COutConsole::Init(WAVEFORMATEX *) {

	static char buffer[1024 * 1024];

	//if (!setvbuf(stdout, NULL, _IOFBF, 1024 * 1024)) {
		//ERR(L"error setvbuf");
	//}

	_setmode(_fileno(stdout), _O_BINARY);
	

	return S_OK;
}

HRESULT COutConsole::ProcessBuffer(BYTE * pData , size_t lBytesToWrite, UINT32 ) {

	
	if (fwrite(pData, 1, lBytesToWrite, stdout) != (size_t)lBytesToWrite) {
		ERR("fwrite error");
	}

	return S_OK;

}

int COutConsole::DeInit(UINT32 ) {

	
	return S_OK;


}
