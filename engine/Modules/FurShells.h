#pragma once

#include <d3d11.h>
#include <winrt/base.h>

namespace FurShells
{
	void Initialize();
	void SetShellLength(float a_lengthGameUnits) noexcept;
	void InstallD3DContextHooks(ID3D11DeviceContext* a_context);
	void ArmForNextDraw(bool a_enabled, bool a_modelSpaceNormals = false, bool a_skinned = false);
	ID3D11GeometryShader* GetShader() noexcept;
}
