#include "ChunkGenerationTaskManager.h"
#include "..\..\NPC\BasicNPC\BasicNPC.h"
#include "..\..\Noise\PerlinNoiseSettings.h"
#include "..\..\Utils\CustomMesh\CustomProceduralMeshComponent.h"
#include "..\ChunkData\ChunkLocationData.h"
#include "..\ChunkMeshThreads\ChunkMeshDataRunnable.h"
#include "..\ChunkMeshThreads\ChunksLocationRunnable.h"
#include "..\TerrainSettings\WorldTerrainSettings.h"
#include "..\Vegetation\Trees\Tree.h"
#include "HAL/RunnableThread.h"

FChunkGenerationTaskManager::FChunkGenerationTaskManager() = default;

FChunkGenerationTaskManager::~FChunkGenerationTaskManager() {
	Shutdown();
}

bool FChunkGenerationTaskManager::TryStartLocationTask(
    const FVector& PlayerPosition,
    UWorldTerrainSettings* WorldTerrainSettings,
    UChunkLocationData* ChunkLocationData,
    TQueue<UCustomProceduralMeshComponent*>* GrassActorsToRemove,
    TQueue<UCustomProceduralMeshComponent*>* FlowerActorsToRemove,
    TQueue<ATree*>* TreeActorsToRemove,
    TQueue<ABasicNPC*>* NpcActorsToRemove
) {
	if (bLocationTaskRunning) {
		return false;
	}

	const bool isWorldDataMissing = !WorldTerrainSettings || !ChunkLocationData || !GrassActorsToRemove || !FlowerActorsToRemove || !TreeActorsToRemove || !NpcActorsToRemove;

	if (isWorldDataMissing) {
		UE_LOG(LogTemp, Error, TEXT("Cannot start chunk location task because required world data is missing"));
		return false;
	}

	LocationRunnable = MakeUnique<ChunksLocationRunnable>(
	    PlayerPosition,
	    WorldTerrainSettings,
	    ChunkLocationData,
	    GrassActorsToRemove,
	    FlowerActorsToRemove,
	    TreeActorsToRemove,
	    NpcActorsToRemove
	);

	LocationThread.Reset(FRunnableThread::Create(LocationRunnable.Get(), TEXT("chunksLocationThread"), 0, TPri_Normal));

	if (!LocationThread) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create chunksLocationThread"));
		LocationRunnable.Reset();
		return false;
	}

	bLocationTaskRunning = true;
	return true;
}

bool FChunkGenerationTaskManager::CompleteLocationTaskIfReady() {
	if (!LocationRunnable || !LocationRunnable->IsTaskComplete()) {
		return false;
	}

	LocationRunnable->Stop();

	if (LocationThread) {
		LocationThread->WaitForCompletion();
		LocationThread.Reset();
	}

	LocationRunnable.Reset();
	bLocationTaskRunning = false;
	return true;
}

bool FChunkGenerationTaskManager::TryStartMeshTask(UWorldTerrainSettings* WorldTerrainSettings, UChunkLocationData* ChunkLocationData, APerlinNoiseSettings* PerlinNoiseSettings) {
	if (bMeshTaskRunning) {
		return false;
	}

	const bool bIsWorldDataMissing = !WorldTerrainSettings || !ChunkLocationData || !PerlinNoiseSettings;

	if (bIsWorldDataMissing) {
		UE_LOG(LogTemp, Error, TEXT("Cannot start chunk mesh task because required world data is missing"));
		return false;
	}

	FVoxelObjectLocationData ChunkToSpawnPosition;
	const bool bDoesSpawnPositionExist = ChunkLocationData->getChunkToSpawnPosition(ChunkToSpawnPosition);
	if (!bDoesSpawnPositionExist) {
		return false;
	}

	MeshDataRunnable = MakeUnique<ChunkMeshDataRunnable>(ChunkToSpawnPosition, WorldTerrainSettings, ChunkLocationData, PerlinNoiseSettings);
	MeshDataThread.Reset(FRunnableThread::Create(MeshDataRunnable.Get(), TEXT("chunkMeshDataThread"), 0, TPri_Normal));

	if (!MeshDataThread) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create chunkMeshDataThread"));
		MeshDataRunnable.Reset();
		ChunkLocationData->AddChunksToSpawnPosition(ChunkToSpawnPosition);
		return false;
	}

	bMeshTaskRunning = true;
	return true;
}

bool FChunkGenerationTaskManager::CompleteMeshTaskIfReady() {
	if (!MeshDataRunnable || !MeshDataRunnable->IsTaskComplete()) {
		return false;
	}

	MeshDataRunnable->Stop();

	if (MeshDataThread) {
		MeshDataThread->WaitForCompletion();
		MeshDataThread.Reset();
	}

	MeshDataRunnable.Reset();
	bMeshTaskRunning = false;
	return true;
}

void FChunkGenerationTaskManager::Shutdown() {
	if (LocationRunnable) {
		LocationRunnable->Stop();
	}

	if (LocationThread) {
		LocationThread->WaitForCompletion();
		LocationThread.Reset();
	}

	LocationRunnable.Reset();
	bLocationTaskRunning = false;

	if (MeshDataRunnable) {
		MeshDataRunnable->Stop();
	}

	if (MeshDataThread) {
		MeshDataThread->WaitForCompletion();
		MeshDataThread.Reset();
	}

	MeshDataRunnable.Reset();
	bMeshTaskRunning = false;
}
