#pragma once

#include "Containers/Queue.h"
#include "CoreMinimal.h"
// #include "..\..\NPC\BasicNPC\BasicNPC.h"
#include "Misc/QueuedThreadPool.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ABasicNPC;
class UChunkLocationData;
class UWorldTerrainSettings;

class PathfindingThreadManager {

  public:
	PathfindingThreadManager(UWorldTerrainSettings* InWorldTerrainSettings, UChunkLocationData* InChunkLocationData, const int& NumThreads);
	~PathfindingThreadManager();

	void ShutDownThreadPool();

	void SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings);
	void SetChunkLocationData(UChunkLocationData* InChunkLocationData);

	// Adds a task to the thread pool
	void AddPathfindingTask(ABasicNPC* npcRef, FVector& startLocation, FVector& endLocation);

  private:
	TWeakObjectPtr<UWorldTerrainSettings> WorldTerrainSettingsRef;

	TWeakObjectPtr<UChunkLocationData> ChunkLocationDataRef;

	TUniquePtr<FQueuedThreadPool> PathfindingThreadPool;

	// Thread management
	bool bThreadPoolRunning;
};
