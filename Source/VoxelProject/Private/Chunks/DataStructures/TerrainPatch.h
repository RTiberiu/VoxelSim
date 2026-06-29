#pragma once

#include "CoreMinimal.h"

struct FTerrainSurfacePoint {
	int32 X{0};
	int32 Z{0};
	int32 Height{0};
};

struct FTerrainPatch {
	FIntPoint ChunkCoordinates{0, 0};
	FVector ChunkWorldLocation{FVector::ZeroVector};
	int32 ChunkSizePadding{0};
	TArray<int32> PaddedHeights;
	TArray<FTerrainSurfacePoint> SurfacePoints;

	bool IsValid() const {
		return ChunkSizePadding > 0 && PaddedHeights.Num() == ChunkSizePadding * ChunkSizePadding;
	}

	int32 GetHeight(const int32 X, const int32 Z) const {
		const int32 HeightIndex = X * ChunkSizePadding + Z;
		return PaddedHeights.IsValidIndex(HeightIndex) ? PaddedHeights[HeightIndex] : 0;
	}
};
