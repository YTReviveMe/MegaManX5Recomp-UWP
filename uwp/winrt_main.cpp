#include <Windows.h>
#include <SDL_main.h>
#include <wrl.h>
#pragma warning(disable : 4447)
int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return SDL_WinRTRunApp(SDL_main, nullptr); }
