#pragma once
// Application UI: 5-step wizard (Project -> Trim -> Media -> Prompt -> Edit).
#include <windows.h>

struct ID3D11Device;

void AppInit(HWND hwnd, ID3D11Device* device, float dpiScale);
void AppFrame();     // build the ImGui frame
void AppShutdown();
