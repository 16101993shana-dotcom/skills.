#pragma once
#include "CoreMinimal.h"
class FMemoryWriter : public FArchive { public: TArray<uint8>& Bytes; explicit FMemoryWriter(TArray<uint8>& In) : Bytes(In) { bLoading = false; }
	void Serialize(void* Data, int64 Num) override { const uint8* P = static_cast<const uint8*>(Data); Bytes.V.insert(Bytes.V.end(), P, P + Num); } };
