#include "PathfindingTask.h"
#include "..\..\Chunks\ChunkData\ChunkLocationData.h"
#include "..\..\Chunks\TerrainSettings\WorldTerrainSettings.h"
#include "..\..\NPC\BasicNPC\BasicNPC.h"
#include "Misc/DateTime.h"

FPathfindingTask::FPathfindingTask(
    const FVector& InStartLocation,
    const FVector& InEndLocation,
    ABasicNPC* InNPCRef,
    UWorldTerrainSettings* InWorldTerrainSettingsRef,
    UChunkLocationData* InChunkLocationDataRef
)
    : WorldTerrainSettingsRef(InWorldTerrainSettingsRef),
      ChunkLocationDataRef(InChunkLocationDataRef),
      NPCRef(InNPCRef),
      StartLocation(InStartLocation),
      EndLocation(InEndLocation),
      bIsSearching(false) {
}

FPathfindingTask::~FPathfindingTask() {
}

void FPathfindingTask::DoThreadedWork() {
	AdjustLocationsToUnrealScaling();

	TUniquePtr<Path> PathToTarget = GetPathToEndLocation();

	if (PathToTarget.IsValid()) {
		AdjustPathWithActualVoxelHeights(*PathToTarget);
	}

	DispatchPathToGameThread(MoveTemp(PathToTarget));
	delete this;
}

void FPathfindingTask::Abandon() {
	if (bIsSearching) {
		FScopeLock SearchProblemLock(&SearchProblemCriticalSection);
		if (SearchProblem.IsValid()) {
			SearchProblem->StopSearching();
		}
	}

	delete this;
}

void FPathfindingTask::SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings) {
	WorldTerrainSettingsRef = InWorldTerrainSettings;
}

void FPathfindingTask::SetChunkLocationData(UChunkLocationData* InChunkLocationData) {
	ChunkLocationDataRef = InChunkLocationData;
}

// Makes each unit in the start and end locaiton be the equivalent of a voxel size
void FPathfindingTask::AdjustLocationsToUnrealScaling() {
	UWorldTerrainSettings* WorldTerrainSettings = WorldTerrainSettingsRef.Get();
	if (!WorldTerrainSettings) {
		return;
	}

	StartLocation = FVector(FMath::FloorToDouble(StartLocation.X / WorldTerrainSettings->UnrealScale), FMath::FloorToDouble(StartLocation.Y / WorldTerrainSettings->UnrealScale), 0);
	EndLocation = FVector(FMath::FloorToDouble(EndLocation.X / WorldTerrainSettings->UnrealScale), FMath::FloorToDouble(EndLocation.Y / WorldTerrainSettings->UnrealScale), 0);
}

TUniquePtr<Path> FPathfindingTask::GetPathToEndLocation() {
	UChunkLocationData* ChunkLocationData = ChunkLocationDataRef.Get();
	if (!ChunkLocationData) {
		return nullptr;
	}

	VoxelSearchState startPosition = VoxelSearchState(StartLocation, ChunkLocationData);
	VoxelSearchState endPosition = VoxelSearchState(EndLocation, ChunkLocationData);

	{
		FScopeLock SearchProblemLock(&SearchProblemCriticalSection);
		SearchProblem = MakeUnique<VoxelSearchProblem>(startPosition, endPosition);
	}

	bIsSearching.AtomicSet(true);
	TUniquePtr<Path> PathToGoal(SearchProblem->search());
	bIsSearching.AtomicSet(false);

	{
		FScopeLock SearchProblemLock(&SearchProblemCriticalSection);
		SearchProblem.Reset();
	}

	return PathToGoal;
}

void FPathfindingTask::AdjustPathWithActualVoxelHeights(Path& PathToAdjust) {
	UChunkLocationData* ChunkLocationData = ChunkLocationDataRef.Get();
	UWorldTerrainSettings* WorldTerrainSettings = WorldTerrainSettingsRef.Get();
	if (!ChunkLocationData || !WorldTerrainSettings) {
		return;
	}

	TMap<FIntPoint, TArray<int>> surfaceVoxelPoints = ChunkLocationData->GetSurfaceVoxelPoints();

	// Update each ActionStatePair in the path
	for (ActionStatePair* pair : PathToAdjust.path) {
		FVector& location = pair->state->getPosition();

		FIntPoint chunkPosition(FMath::FloorToInt(location.X / WorldTerrainSettings->chunkSize), FMath::FloorToInt(location.Y / WorldTerrainSettings->chunkSize));

		if (surfaceVoxelPoints.Contains(chunkPosition)) {
			const TArray<int>& heights = surfaceVoxelPoints[chunkPosition];

			const int modX = FMath::Max(((static_cast<int>(location.X) % WorldTerrainSettings->chunkSize) + WorldTerrainSettings->chunkSize) % WorldTerrainSettings->chunkSize - 1, 0);
			const int modY = FMath::Max(((static_cast<int>(location.Y) % WorldTerrainSettings->chunkSize) + WorldTerrainSettings->chunkSize) % WorldTerrainSettings->chunkSize - 1, 0);

			const int index = modX * WorldTerrainSettings->chunkSize + modY;

			if (heights.IsValidIndex(index)) {
				location.Z = heights[index] * WorldTerrainSettings->UnrealScale;
				location.X = location.X * WorldTerrainSettings->UnrealScale + WorldTerrainSettings->HalfUnrealScale;
				location.Y = location.Y * WorldTerrainSettings->UnrealScale + WorldTerrainSettings->HalfUnrealScale;
			}
		}
	}
}

void FPathfindingTask::DispatchPathToGameThread(TUniquePtr<Path> PathToTarget) {
	TWeakObjectPtr<ABasicNPC> WeakNPC = NPCRef;

	AsyncTask(ENamedThreads::GameThread, [WeakNPC, PathToTarget = MoveTemp(PathToTarget)]() mutable {
		if (!WeakNPC.IsValid()) {
			return;
		}

		WeakNPC->SetPathToTargetAndNotify(MoveTemp(PathToTarget));
	});
}

// Method used for testing. It prints all the heights in the current chunk, to better visualize their positions
void FPathfindingTask::PrintHeights(const TArray<int>& heights) {
	FString output;
	output += TEXT("CHUNK: \n");

	for (int i = 0; i < heights.Num(); ++i) {
		output += FString::FromInt(heights[i]) + TEXT("\t");
		if ((i + 1) % 62 == 0) {
			output += TEXT("\n");
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("%s"), *output);
}