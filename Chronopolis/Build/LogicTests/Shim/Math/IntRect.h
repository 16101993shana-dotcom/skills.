#pragma once
#include "CoreMinimal.h"
struct FIntRect { FIntPoint Min, Max; FIntRect() = default; FIntRect(int32 X0, int32 Y0, int32 X1, int32 Y1) : Min(X0, Y0), Max(X1, Y1) {}
	int32 Width() const { return Max.X - Min.X; } int32 Height() const { return Max.Y - Min.Y; } };
