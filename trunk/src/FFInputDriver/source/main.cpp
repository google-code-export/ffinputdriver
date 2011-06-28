#include <vd2/plugin/vdplugin.h>
#include <vd2/plugin/vdinputdriver.h>

#include <windows.h>
#include <tchar.h>


///////////////////////////////////////////////////////////////////////////////

extern const VDPluginInfo ff_plugin;

const VDPluginInfo *const kPlugins[]={
	&ff_plugin,
	NULL
};

extern "C" const VDPluginInfo *const * VDXAPIENTRY VDGetPluginInfo() 
{
	return kPlugins;
}


BOOLEAN WINAPI DllMain( IN HINSTANCE hDllHandle, 
	IN DWORD     nReason, 
	IN LPVOID    Reserved )
{
	BOOLEAN bSuccess = TRUE;

	LPTSTR  strModulePath = new TCHAR[_MAX_PATH];
	::GetModuleFileName(hDllHandle, strModulePath, _MAX_PATH);

	int strLen =  _tcslen(strModulePath);
	while( --strLen > 0 && strModulePath[strLen] != L'/' && strModulePath[strLen] != L'\\' );
	
	strModulePath[strLen + 1] = 0;

	_tcscat(strModulePath, "ffdlls");


	//  Perform global initialization.

	switch ( nReason )
	{
	case DLL_PROCESS_ATTACH:
		SetDllDirectory( strModulePath );

		bSuccess = DisableThreadLibraryCalls( hDllHandle );

		break;

	case DLL_PROCESS_DETACH:

		break;
	}

	return bSuccess;

}
