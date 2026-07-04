#pragma once

#include "..\..\Utils\Semaphore\FairSemaphore.h"

#include "..\..\NPC\BasicNPC\BasicNPC.h"
#include "..\DataStructures\VoxelObjectLocationData.h"
#include "..\DataStructures\VoxelObjectMeshData.h"
#include "..\Vegetation\Trees\Tree.h"
#include "ChunkLocationData.generated.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"

class ABasicNPC;
class WorldTerrainSettings;

struct FChunkMeshBuildResult {
	FVoxelObjectLocationData LocationData;
	FVoxelObjectMeshData MeshData;
};

struct FNavigationSurfaceChunk {
	TArray<int> SurfaceVoxelPoints;
	TArray<FVector2D> SurfaceAvoidPoints;
};

UCLASS()
class UChunkLocationData : public UObject {
	GENERATED_BODY()

  public:
	UChunkLocationData();

	~UChunkLocationData();

	void SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings);

	bool getChunkToSpawnPosition(FVoxelObjectLocationData& OutLocation);
	bool getChunkToDestroyPosition(FIntPoint& OutPosition);

	bool getComputedMeshDataAndLocationData(FVoxelObjectLocationData& locationData, FVoxelObjectMeshData& meshData, const FIntPoint& PriorityChunkPosition);
	bool isMeshWaitingToBeSpawned();

	bool IsChunkSpawnRequested(const FIntPoint& chunkPosition) const;
	void AddChunksToSpawnPosition(const FVoxelObjectLocationData position);
	void AddChunksToDestroyPosition(const FIntPoint& position);
	void AddMeshDataForPosition(const FVoxelObjectLocationData chunkLocationData, const FVoxelObjectMeshData meshData);

	void AddTreeToDestroyPosition(const FIntPoint& tree);
	bool GetTreeToDestroyPosition(FIntPoint& tree);

	void emptyPositionQueues();

	void AddVegetationChunkSpawnPosition(FIntPoint& chunkPosition);
	void AddTreeChunkSpawnPosition(FIntPoint& chunkPosition);
	void AddNpcChunkSpawnPosition(FIntPoint& chunkPosition);
	void RemoveVegetationChunkSpawnPosition(FIntPoint& chunkPosition);
	void RemoveTreeChunkSpawnPosition(FIntPoint& chunkPosition);
	void RemoveNpcChunkSpawnPosition(FIntPoint& chunkPosition);
	void CheckForSpawnPointsInRange();
	void CheckAndAddVegetationNotInRange(
	    TQueue<UCustomProceduralMeshComponent*>* GrassActorsToRemove,
	    TQueue<UCustomProceduralMeshComponent*>* FlowerActorsToRemove
	);
	TSet<FIntPoint> GetVegetationChunkSpawnPoints() const;

	void CheckAndAddTreesNotInRange(TQueue<ATree*>* TreeActorsToRemove);
	void CheckAndAddNpcsNotInRange(TQueue<ABasicNPC*>* NpcActorsToRemove);
	TSet<FIntPoint> GetTreeChunkSpawnPoints() const;

	TArray<FVoxelObjectLocationData> getTreeSpawnPositions();
	TArray<FVoxelObjectLocationData> getGrassSpawnPosition();
	TArray<FVoxelObjectLocationData> getFlowerSpawnPosition();
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> getNPCSpawnPosition();
	TSet<FIntPoint> GetNpcChunkSpawnPoints() const;

	TArray<FVoxelObjectLocationData> getTreeSpawnPositionsInRange(const FIntPoint& PriorityChunkPosition);
	TArray<FVoxelObjectLocationData> getGrassSpawnPositionInRange(const FIntPoint& PriorityChunkPosition);
	TArray<FVoxelObjectLocationData> getFlowerSpawnPositionInRange(const FIntPoint& PriorityChunkPosition);
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> getNPCSpawnPositionInRange(const FIntPoint& PriorityChunkPosition);

	// Methods to add single spawn points for vegetation and NPCs
	void addTreeSpawnPosition(const FVoxelObjectLocationData position);
	void addGrassSpawnPosition(const FVoxelObjectLocationData position);
	void addFlowerSpawnPosition(const FVoxelObjectLocationData position);
	void addNPCSpawnPosition(const TPair<FVoxelObjectLocationData, AnimalType> positionAndType);

	// Methods to add batch spawn points for vegetation and NPCs
	void addTreeSpawnPositions(const TArray<FVoxelObjectLocationData>& positions);
	void addGrassSpawnPositions(const TArray<FVoxelObjectLocationData>& positions);
	void addFlowerSpawnPositions(const TArray<FVoxelObjectLocationData>& positions);
	void addNPCSpawnPositions(const TArray<TPair<FVoxelObjectLocationData, AnimalType>>& positionsAndTypes);

	void RemoveTreeSpawnPosition(const FIntPoint& point);
	void RemoveGrassSpawnPosition(const FIntPoint& point);
	void RemoveFlowerSpawnPosition(const FIntPoint& point);
	void RemoveNPCSpawnPosition(const FIntPoint& point);

	bool AddUnspawnedTreeToDestroy(ATree* InTreeToDestroy);
	bool GetUnspawnedTreeToDestroy(ATree* InTreeToDestroy);

	bool AddUnspawnedGrassToDestroy(UProceduralMeshComponent* InGrassToDestroy);
	bool GetUnspawnedGrassToDestroy(UProceduralMeshComponent* InGrassToDestroy);

	bool AddUnspawnedFlowerToDestroy(UProceduralMeshComponent* InFlowerToDestroy);
	bool GetUnspawnedFlowerToDestroy(UProceduralMeshComponent* InFlowerToDestroy);

	bool AddUnspawnedNpcToDestroy(ABasicNPC* InNpcToDestroy);
	bool GetUnspawnedNpcToDestroy(ABasicNPC* InNpcToDestroy);

	void AddSurfaceVoxelPointsForChunk(const FIntPoint& chunkPosition, const TArray<int>& voxelPoints, const TArray<FVector2D>& avoidPoints);
	void RemoveSurfaceVoxelPointsForChunk(const FIntPoint& chunkPosition);
	TMap<FIntPoint, TArray<int>> GetSurfaceVoxelPoints();

	bool IsSurfacePointValid(const double& X, const double& Z);

	bool IsLocationOccupied(const FVector& currentPosition, const FVector& nextPosition, ABasicNPC* npcAtLocation);
	bool IsLocationOccupied(const FIntPoint& position);
	void AddOccupiedVoxelPosition(const FVector& position, ABasicNPC* npcAtLocation);
	void RemoveOccupiedVoxelPosition(const FVector& position);
	TMap<FIntPoint, ABasicNPC*> GetOccupiedVoxels() const;

  private:
	UWorldTerrainSettings* WorldTerrainSettingsRef;
	UWorldTerrainSettings*& WTSR = WorldTerrainSettingsRef;

	const int chunkSize{62};

	// Tracking chunks that should exist after streaming finishes
	TSet<FIntPoint> RequestedChunkSpawnPositions;
	TUniquePtr<FairSemaphore> ChunkStreamingSemaphore;

	// Queue for storing chunks position that need to be spawned
	TQueue<FVoxelObjectLocationData> chunksToSpawnPositions;

	// Queue for storing chunks position that need to be destroyed
	TQueue<FIntPoint> chunksToDestroyPositions;

	TQueue<ATree*> unspawnedTreesToDestroy;
	TQueue<UProceduralMeshComponent*> unspawnedGrassToDestroy;
	TQueue<UProceduralMeshComponent*> unspawnedFlowerToDestroy;
	TQueue<ABasicNPC*> unspawnedNpcsToDestroy;

	TQueue<FIntPoint> treesToDestroy;
	TQueue<FIntPoint> grassToDestroy;
	TQueue<FIntPoint> flowersToDestroy;
	TQueue<FIntPoint> npcsToDestroy;

	// Queue for storing chunks mesh data
	TArray<FChunkMeshBuildResult> ComputedMeshResults;

	// Queue for storing all vegetation spawn points data (even outside of the LOD range)
	TUniquePtr<FairSemaphore> TreesToSpawnSemaphore;
	TMap<FIntPoint, TArray<FVoxelObjectLocationData>> treesSpawnPositions;

	TUniquePtr<FairSemaphore> GrassToSpawnSemaphore;
	TMap<FIntPoint, TArray<FVoxelObjectLocationData>> grassSpawnPositions;

	TUniquePtr<FairSemaphore> FlowersToSpawnSemaphore;
	TMap<FIntPoint, TArray<FVoxelObjectLocationData>> flowersSpawnPositions;

	TUniquePtr<FairSemaphore> NPCToSpawnSemaphore;
	TMap<FIntPoint, TArray<TPair<FVoxelObjectLocationData, AnimalType>>> npcSpawnPositions;

	// Storing the current chunk points where vegetation should spawn
	TSet<FIntPoint> VegetationChunkSpawnPoints;
	TSet<FIntPoint> TreeChunkSpawnPoints;
	TSet<FIntPoint> NpcChunkSpawnPoints;

	TUniquePtr<FairSemaphore> VegetationChunkSemaphore;
	TUniquePtr<FairSemaphore> TreeChunkSemaphore;
	TUniquePtr<FairSemaphore> NpcChunkSemaphore;

	TSet<FIntPoint> grassInRangeSpawnPositions;
	TSet<FIntPoint> flowersInRangeSpawnPositions;
	TSet<FIntPoint> treesInRangeSpawnPositions;
	TSet<FIntPoint> npcInRangeSpawnPositions;

	TUniquePtr<FairSemaphore> MeshDataSemaphore;

	TUniquePtr<FairSemaphore> SurfaceVoxelPointsSemaphore;
	// Storing navigation surface data per chunk
	TMap<FIntPoint, FNavigationSurfaceChunk> NavigationSurfaceChunks;

	// Map used for avoiding overlapping NPCs during pathfinding and movement
	TMap<FIntPoint, ABasicNPC*> OccupiedVoxels;
};
