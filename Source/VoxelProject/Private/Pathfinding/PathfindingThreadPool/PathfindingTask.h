// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "..\SearchProblem\VoxelSearchProblem.h"
#include "Async/Async.h"
#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ABasicNPC;
class VoxelSearchProblem;
class UWorldTerrainSettings;
class UChunkLocationData;

class FPathfindingTask : public IQueuedWork {
  public:
	FPathfindingTask(
	    const FVector& InStartLocation,
	    const FVector& InEndLocation,
	    ABasicNPC* InNPCRef,
	    UWorldTerrainSettings* InWorldTerrainSettingsRef,
	    UChunkLocationData* InChunkLocationDataRef
	);

	virtual ~FPathfindingTask();

	virtual void DoThreadedWork() override;
	virtual void Abandon() override;

	void SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings);
	void SetChunkLocationData(UChunkLocationData* InChunkLocationData);

  private:
	void AdjustLocationsToUnrealScaling();
	TUniquePtr<Path> GetPathToEndLocation();
	void AdjustPathWithActualVoxelHeights(Path& PathToAdjust);
	void DispatchPathToGameThread(TUniquePtr<Path> PathToTarget);

	void PrintHeights(const TArray<int>& heights); // TESTING THE SURFACE VOXELS

	TWeakObjectPtr<UWorldTerrainSettings> WorldTerrainSettingsRef;

	TWeakObjectPtr<UChunkLocationData> ChunkLocationDataRef;

	TWeakObjectPtr<ABasicNPC> NPCRef;

	FVector StartLocation;
	FVector EndLocation;

	TUniquePtr<VoxelSearchProblem> SearchProblem;
	FCriticalSection SearchProblemCriticalSection;
	FThreadSafeBool bIsSearching;
};
