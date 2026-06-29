#include "PathfindingThreadManager.h"
#include "..\..\Chunks\ChunkData\ChunkLocationData.h"
#include "..\..\Chunks\TerrainSettings\WorldTerrainSettings.h"
#include "PathfindingTask.h"

PathfindingThreadManager::PathfindingThreadManager(UWorldTerrainSettings* InWorldTerrainSettings, UChunkLocationData* InChunkLocationData, const int& NumThreads) {
	bThreadPoolRunning = true;

	SetWorldTerrainSettings(InWorldTerrainSettings);
	SetChunkLocationData(InChunkLocationData);

	PathfindingThreadPool.Reset(FQueuedThreadPool::Allocate());
	bool threadPoolResult = PathfindingThreadPool->Create(NumThreads, 32 * 1024, EThreadPriority::TPri_Normal, TEXT("PathfindingThreadPool"));

	if (threadPoolResult) {
		UE_LOG(LogTemp, Warning, TEXT("Thread pool created with %d threads."), NumThreads);
	} else {
		UE_LOG(LogTemp, Error, TEXT("Failed to create FQueuedThreadPool!"));
		PathfindingThreadPool.Reset();
		bThreadPoolRunning = false;
	}
}

PathfindingThreadManager::~PathfindingThreadManager() {
	ShutDownThreadPool();
}

void PathfindingThreadManager::ShutDownThreadPool() {
	if (!PathfindingThreadPool.IsValid()) {
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Started the thread pool destroy process."));
	PathfindingThreadPool->Destroy();
	PathfindingThreadPool.Reset();
	bThreadPoolRunning = false;
	UE_LOG(LogTemp, Warning, TEXT("Thread pool destroyed succesfully."));
}

void PathfindingThreadManager::SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings) {
	WorldTerrainSettingsRef = InWorldTerrainSettings;
}

void PathfindingThreadManager::SetChunkLocationData(UChunkLocationData* InChunkLocationData) {
	ChunkLocationDataRef = InChunkLocationData;
}

void PathfindingThreadManager::AddPathfindingTask(ABasicNPC* npcRef, FVector& startLocation, FVector& endLocation) {
	if (!PathfindingThreadPool.IsValid()) {
		UE_LOG(LogTemp, Error, TEXT("Thread pool is not initialized!"));
		return;
	}

	UWorldTerrainSettings* WorldTerrainSettings = WorldTerrainSettingsRef.Get();
	UChunkLocationData* ChunkLocationData = ChunkLocationDataRef.Get();
	if (!WorldTerrainSettings || !ChunkLocationData) {
		UE_LOG(LogTemp, Error, TEXT("Pathfinding settings are invalid."));
		return;
	}

	TUniquePtr<FPathfindingTask> NewTask = MakeUnique<FPathfindingTask>(startLocation, endLocation, npcRef, WorldTerrainSettings, ChunkLocationData);
	PathfindingThreadPool->AddQueuedWork(NewTask.Release());
}
