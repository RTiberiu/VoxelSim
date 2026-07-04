#pragma once

#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class ABasicNPC;
class APerlinNoiseSettings;
class ATree;
class ChunkMeshDataRunnable;
class ChunksLocationRunnable;
class FRunnableThread;
class UChunkLocationData;
class UCustomProceduralMeshComponent;
class UWorldTerrainSettings;

class FChunkGenerationTaskManager final {
  public:
	FChunkGenerationTaskManager();
	~FChunkGenerationTaskManager();

	bool TryStartLocationTask(
	    const FVector& PlayerPosition,
	    UWorldTerrainSettings* WorldTerrainSettings,
	    UChunkLocationData* ChunkLocationData,
	    TQueue<UCustomProceduralMeshComponent*>* GrassActorsToRemove,
	    TQueue<UCustomProceduralMeshComponent*>* FlowerActorsToRemove,
	    TQueue<ATree*>* TreeActorsToRemove,
	    TQueue<ABasicNPC*>* NpcActorsToRemove
	);

	bool CompleteLocationTaskIfReady();
	bool TryStartMeshTask(UWorldTerrainSettings* WorldTerrainSettings, UChunkLocationData* ChunkLocationData, APerlinNoiseSettings* PerlinNoiseSettings);
	bool CompleteMeshTaskIfReady();

	void Shutdown();

  private:
	TUniquePtr<ChunksLocationRunnable> LocationRunnable;
	TUniquePtr<FRunnableThread> LocationThread;
	bool bLocationTaskRunning = false;

	TUniquePtr<ChunkMeshDataRunnable> MeshDataRunnable;
	TUniquePtr<FRunnableThread> MeshDataThread;
	bool bMeshTaskRunning = false;
};
