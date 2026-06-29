#include "ChunkLocationData.h"

#include "..\TerrainSettings\WorldTerrainSettings.h"

UChunkLocationData::UChunkLocationData()
    : ChunksToSpawnSemaphore(MakeUnique<FairSemaphore>(1)),
      ChunksToDestroySemaphore(MakeUnique<FairSemaphore>(1)),
      TreesToSpawnSemaphore(MakeUnique<FairSemaphore>(1)),
      GrassToSpawnSemaphore(MakeUnique<FairSemaphore>(1)),
      FlowersToSpawnSemaphore(MakeUnique<FairSemaphore>(1)),
      NPCToSpawnSemaphore(MakeUnique<FairSemaphore>(1)),
      VegetationChunkSemaphore(MakeUnique<FairSemaphore>(1)),
      TreeChunkSemaphore(MakeUnique<FairSemaphore>(1)),
      NpcChunkSemaphore(MakeUnique<FairSemaphore>(1)),
      MeshDataSemaphore(MakeUnique<FairSemaphore>(1)),
      SurfaceVoxelPointsSemaphore(MakeUnique<FairSemaphore>(1)) {
}

UChunkLocationData::~UChunkLocationData() {
}

void UChunkLocationData::SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings) {
	WorldTerrainSettingsRef = InWorldTerrainSettings;
}

bool UChunkLocationData::getChunkToSpawnPosition(FVoxelObjectLocationData& OutLocation) {
	return chunksToSpawnPositions.Dequeue(OutLocation);
}

bool UChunkLocationData::getChunkToDestroyPosition(FIntPoint& OutPosition) {
	return chunksToDestroyPositions.Dequeue(OutPosition);
}

bool UChunkLocationData::getComputedMeshDataAndLocationData(FVoxelObjectLocationData& locationData, FVoxelObjectMeshData& meshData) {
	MeshDataSemaphore->Acquire();
	FChunkMeshBuildResult MeshBuildResult;
	const bool bRemovedMeshData = ComputedMeshResults.Dequeue(MeshBuildResult);
	MeshDataSemaphore->Release();

	if (!bRemovedMeshData) {
		return false;
	}

	locationData = MeshBuildResult.LocationData;
	meshData = MeshBuildResult.MeshData;
	return true;
}

bool UChunkLocationData::isMeshWaitingToBeSpawned() {
	MeshDataSemaphore->Acquire();
	const bool bIsMeshWaiting = !ComputedMeshResults.IsEmpty();
	MeshDataSemaphore->Release();
	return bIsMeshWaiting;
}

void UChunkLocationData::AddChunksToSpawnPosition(const FVoxelObjectLocationData position) {
	chunksToSpawnPositions.Enqueue(position);
}

void UChunkLocationData::AddChunksToDestroyPosition(const FIntPoint& position) {
	chunksToDestroyPositions.Enqueue(position);
}

void UChunkLocationData::AddMeshDataForPosition(const FVoxelObjectLocationData chunkLocationData, const FVoxelObjectMeshData meshData) {
	MeshDataSemaphore->Acquire();
	ComputedMeshResults.Enqueue(FChunkMeshBuildResult{chunkLocationData, meshData});
	MeshDataSemaphore->Release();
}

void UChunkLocationData::AddTreeToDestroyPosition(const FIntPoint& treePosition) {
	treesToDestroy.Enqueue(treePosition);
}

bool UChunkLocationData::GetTreeToDestroyPosition(FIntPoint& treePosition) {
	return treesToDestroy.Dequeue(treePosition);
}

void UChunkLocationData::emptyPositionQueues() {
	chunksToSpawnPositions.Empty();
	chunksToDestroyPositions.Empty();
}

void UChunkLocationData::AddVegetationChunkSpawnPosition(FIntPoint& chunkPosition) {
	VegetationChunkSemaphore->Acquire();
	VegetationChunkSpawnPoints.Add(chunkPosition);
	VegetationChunkSemaphore->Release();
}

void UChunkLocationData::AddTreeChunkSpawnPosition(FIntPoint& chunkPosition) {
	TreeChunkSemaphore->Acquire();
	TreeChunkSpawnPoints.Add(chunkPosition);
	TreeChunkSemaphore->Release();
}

void UChunkLocationData::AddNpcChunkSpawnPosition(FIntPoint& chunkPosition) {
	NpcChunkSemaphore->Acquire();
	NpcChunkSpawnPoints.Add(chunkPosition);
	NpcChunkSemaphore->Release();
}

void UChunkLocationData::RemoveVegetationChunkSpawnPosition(FIntPoint& chunkPosition) {
	VegetationChunkSemaphore->Acquire();
	if (VegetationChunkSpawnPoints.Contains(chunkPosition)) {
		VegetationChunkSpawnPoints.Remove(chunkPosition);
	}

	// Remove grass, flowers, and trees waiting to be spawned
	if (grassInRangeSpawnPositions.Contains(chunkPosition)) {
		grassInRangeSpawnPositions.Remove(chunkPosition);
	}

	if (flowersInRangeSpawnPositions.Contains(chunkPosition)) {
		flowersInRangeSpawnPositions.Remove(chunkPosition);
	}

	VegetationChunkSemaphore->Release();
}

void UChunkLocationData::RemoveTreeChunkSpawnPosition(FIntPoint& chunkPosition) {
	TreeChunkSemaphore->Acquire();
	if (TreeChunkSpawnPoints.Contains(chunkPosition)) {
		TreeChunkSpawnPoints.Remove(chunkPosition);
	}

	if (treesInRangeSpawnPositions.Contains(chunkPosition)) {
		treesInRangeSpawnPositions.Remove(chunkPosition);
	}

	TreeChunkSemaphore->Release();
}

void UChunkLocationData::RemoveNpcChunkSpawnPosition(FIntPoint& chunkPosition) {
	NpcChunkSemaphore->Acquire();
	if (NpcChunkSpawnPoints.Contains(chunkPosition)) {
		NpcChunkSpawnPoints.Remove(chunkPosition);
	}

	if (npcInRangeSpawnPositions.Contains(chunkPosition)) {
		npcInRangeSpawnPositions.Remove(chunkPosition);
	}
	NpcChunkSemaphore->Release();
}

TSet<FIntPoint> UChunkLocationData::GetVegetationChunkSpawnPoints() const {
	VegetationChunkSemaphore->Acquire();
	TSet<FIntPoint> result = VegetationChunkSpawnPoints;
	VegetationChunkSemaphore->Release();
	return result;
}

void UChunkLocationData::CheckForSpawnPointsInRange() {
	VegetationChunkSemaphore->Acquire();

	// Check if Vegetation spawn points are in Grass spawn points, and add a reference
	// to that list of Grass points if it exists.
	for (const FIntPoint& VegetationChunkPosition : VegetationChunkSpawnPoints) {
		// Adding grass spawn points
		GrassToSpawnSemaphore->Acquire();
		if (grassSpawnPositions.Contains(VegetationChunkPosition)) {
			if (!grassInRangeSpawnPositions.Contains(VegetationChunkPosition)) {
				grassInRangeSpawnPositions.Add(VegetationChunkPosition);
			}
		}
		GrassToSpawnSemaphore->Release();

		// Adding flower spawn points
		FlowersToSpawnSemaphore->Acquire();
		if (flowersSpawnPositions.Contains(VegetationChunkPosition)) {
			if (!flowersInRangeSpawnPositions.Contains(VegetationChunkPosition)) {
				flowersInRangeSpawnPositions.Add(VegetationChunkPosition);
			}
		}
		FlowersToSpawnSemaphore->Release();

		// Adding trees spawn points // COMMENTED OUT TO KEEP THEM OUT OF THE LOD SYSTEM
		if (flowersSpawnPositions.Contains(VegetationChunkPosition)) {
			if (!flowersInRangeSpawnPositions.Contains(VegetationChunkPosition)) {
				flowersInRangeSpawnPositions.Add(VegetationChunkPosition);
			}
		}
	}

	VegetationChunkSemaphore->Release();

	// Checking the tree spawn area
	TreeChunkSemaphore->Acquire();
	TreesToSpawnSemaphore->Acquire();
	for (const FIntPoint& TreeChunkPosition : TreeChunkSpawnPoints) {
		if (treesSpawnPositions.Contains(TreeChunkPosition)) {
			if (!treesInRangeSpawnPositions.Contains(TreeChunkPosition)) {
				treesInRangeSpawnPositions.Add(TreeChunkPosition);
			}
		}
	}
	TreesToSpawnSemaphore->Release();
	TreeChunkSemaphore->Release();

	// Checking the NPC spawn area
	NpcChunkSemaphore->Acquire();
	NPCToSpawnSemaphore->Acquire();
	for (const FIntPoint& NpcChunkPosition : NpcChunkSpawnPoints) {
		if (npcSpawnPositions.Contains(NpcChunkPosition)) {
			if (!npcInRangeSpawnPositions.Contains(NpcChunkPosition)) {
				npcInRangeSpawnPositions.Add(NpcChunkPosition);
			}
		}
	}
	NPCToSpawnSemaphore->Release();
	NpcChunkSemaphore->Release();
}

void UChunkLocationData::CheckAndAddVegetationNotInRange(
    TQueue<UCustomProceduralMeshComponent*>* GrassActorsToRemove,
    TQueue<UCustomProceduralMeshComponent*>* FlowerActorsToRemove
) {
	VegetationChunkSemaphore->Acquire();

	// Getting the chunk coordinates from vegetation chunk spawn points
	TArray<FIntPoint> Keys = VegetationChunkSpawnPoints.Array();

	WTSR->CheckAndReturnGrassNotInRange(Keys, GrassActorsToRemove);
	WTSR->CheckAndReturnFlowersNotInRange(Keys, FlowerActorsToRemove);

	VegetationChunkSemaphore->Release();
}

void UChunkLocationData::CheckAndAddTreesNotInRange(TQueue<ATree*>* TreeActorsToRemove) {
	TreeChunkSemaphore->Acquire();

	// Getting the chunk coordinates from tree chunk spawn points
	TArray<FIntPoint> Keys = TreeChunkSpawnPoints.Array();

	WTSR->CheckAndReturnTreesNotInRange(Keys, TreeActorsToRemove);

	TreeChunkSemaphore->Release();
}

void UChunkLocationData::CheckAndAddNpcsNotInRange(TQueue<ABasicNPC*>* NpcActorsToRemove) {
	NpcChunkSemaphore->Acquire();

	// Getting the chunk coordinates from NPC chunk spawn points
	TArray<FIntPoint> Keys = NpcChunkSpawnPoints.Array();

	WTSR->CheckAndReturnNpcsNotInRange(Keys, NpcActorsToRemove);

	NpcChunkSemaphore->Release();
}

TSet<FIntPoint> UChunkLocationData::GetTreeChunkSpawnPoints() const {
	TreeChunkSemaphore->Acquire();
	TSet<FIntPoint> result = TreeChunkSpawnPoints;
	TreeChunkSemaphore->Release();
	return result;
}

TSet<FIntPoint> UChunkLocationData::GetNpcChunkSpawnPoints() const {
	NpcChunkSemaphore->Acquire();
	TSet<FIntPoint> result = NpcChunkSpawnPoints;
	NpcChunkSemaphore->Release();
	return result;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getTreeSpawnPositions() {
	TArray<FVoxelObjectLocationData> output;
	TreesToSpawnSemaphore->Acquire();

	// Get the first item from the map // TODO Might be worth replacing the map with a Queue
	if (!treesSpawnPositions.IsEmpty()) {
		for (const TPair<FIntPoint, TArray<FVoxelObjectLocationData>>& pair : treesSpawnPositions) {
			output = pair.Value;
			treesSpawnPositions.Remove(pair.Key);
			break;
		}
	}

	TreesToSpawnSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getGrassSpawnPosition() {
	TArray<FVoxelObjectLocationData> output;
	GrassToSpawnSemaphore->Acquire();

	// Get the first item from the map // TODO Might be worth replacing the map with a Queue
	if (!grassSpawnPositions.IsEmpty()) {
		for (const TPair<FIntPoint, TArray<FVoxelObjectLocationData>>& pair : grassSpawnPositions) {
			output = pair.Value;
			grassSpawnPositions.Remove(pair.Key);
			break;
		}
	}

	GrassToSpawnSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getFlowerSpawnPosition() {
	TArray<FVoxelObjectLocationData> output;
	FlowersToSpawnSemaphore->Acquire();

	// Get the first item from the map // TODO Might be worth replacing the map with a Queue
	if (!flowersSpawnPositions.IsEmpty()) {
		for (const TPair<FIntPoint, TArray<FVoxelObjectLocationData>>& pair : flowersSpawnPositions) {
			output = pair.Value;
			flowersSpawnPositions.Remove(pair.Key);
			break;
		}
	}

	FlowersToSpawnSemaphore->Release();
	return output;
}

TArray<TPair<FVoxelObjectLocationData, AnimalType>> UChunkLocationData::getNPCSpawnPosition() {
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> output;

	NPCToSpawnSemaphore->Acquire();

	// Get the first item from the map // TODO Maybe extractt this into a function, as all getSpawnPosition use the same logic
	if (!npcSpawnPositions.IsEmpty()) {
		for (const TPair<FIntPoint, TArray<TPair<FVoxelObjectLocationData, AnimalType>>>& pair : npcSpawnPositions) {
			output = pair.Value;
			npcSpawnPositions.Remove(pair.Key);
			break;
		}
	}

	NPCToSpawnSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getTreeSpawnPositionsInRange() {
	TArray<FVoxelObjectLocationData> output;
	TreeChunkSemaphore->Acquire();
	TreesToSpawnSemaphore->Acquire();

	FIntPoint KeyToRemove = FIntPoint::ZeroValue;
	bool bFoundSpawnPositions = false;

	for (const FIntPoint& ChunkPosition : treesInRangeSpawnPositions) {
		if (TArray<FVoxelObjectLocationData>* SpawnPositions = treesSpawnPositions.Find(ChunkPosition)) {
			if (SpawnPositions->Num() > 0) {
				output = *SpawnPositions;
				SpawnPositions->Empty();

				// Storing the key to remove it from the map
				KeyToRemove = ChunkPosition;
				bFoundSpawnPositions = true;
				break;
			}
		}
	}

	if (bFoundSpawnPositions) {
		treesInRangeSpawnPositions.Remove(KeyToRemove);
	}

	TreesToSpawnSemaphore->Release();
	TreeChunkSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getGrassSpawnPositionInRange() {
	TArray<FVoxelObjectLocationData> output;
	VegetationChunkSemaphore->Acquire();
	GrassToSpawnSemaphore->Acquire();

	FIntPoint KeyToRemove = FIntPoint::ZeroValue;
	bool bFoundSpawnPositions = false;

	for (const FIntPoint& ChunkPosition : grassInRangeSpawnPositions) {
		if (TArray<FVoxelObjectLocationData>* SpawnPositions = grassSpawnPositions.Find(ChunkPosition)) {
			if (SpawnPositions->Num() > 0) {
				output = *SpawnPositions;

				// Storing the key to remove it from the map
				KeyToRemove = ChunkPosition;
				bFoundSpawnPositions = true;

				SpawnPositions->Empty();
				break;
			}
		}
	}

	if (bFoundSpawnPositions) {
		grassInRangeSpawnPositions.Remove(KeyToRemove);
	}

	GrassToSpawnSemaphore->Release();
	VegetationChunkSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getFlowerSpawnPositionInRange() {
	TArray<FVoxelObjectLocationData> output;
	VegetationChunkSemaphore->Acquire();
	FlowersToSpawnSemaphore->Acquire();

	FIntPoint KeyToRemove = FIntPoint::ZeroValue;
	bool bFoundSpawnPositions = false;

	for (const FIntPoint& ChunkPosition : flowersInRangeSpawnPositions) {
		if (TArray<FVoxelObjectLocationData>* SpawnPositions = flowersSpawnPositions.Find(ChunkPosition)) {
			if (SpawnPositions->Num() > 0) {
				output = *SpawnPositions;
				SpawnPositions->Empty();

				// Storing the key to remove it from the map
				KeyToRemove = ChunkPosition;
				bFoundSpawnPositions = true;
				break;
			}
		}
	}

	if (bFoundSpawnPositions) {
		flowersInRangeSpawnPositions.Remove(KeyToRemove);
	}

	FlowersToSpawnSemaphore->Release();
	VegetationChunkSemaphore->Release();
	return output;
}

TArray<TPair<FVoxelObjectLocationData, AnimalType>> UChunkLocationData::getNPCSpawnPositionInRange() {
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> output;
	NpcChunkSemaphore->Acquire();
	NPCToSpawnSemaphore->Acquire();

	FIntPoint KeyToRemove = FIntPoint::ZeroValue;
	bool bFoundSpawnPositions = false;

	for (const FIntPoint& ChunkPosition : npcInRangeSpawnPositions) {
		if (TArray<TPair<FVoxelObjectLocationData, AnimalType>>* SpawnPositions = npcSpawnPositions.Find(ChunkPosition)) {
			if (SpawnPositions->Num() > 0) {
				output = *SpawnPositions;
				SpawnPositions->Empty();

				// Storing the key to remove it from the map
				KeyToRemove = ChunkPosition;
				bFoundSpawnPositions = true;
				break;
			}
		}
	}

	if (bFoundSpawnPositions) {
		npcInRangeSpawnPositions.Remove(KeyToRemove);
	}

	NPCToSpawnSemaphore->Release();
	NpcChunkSemaphore->Release();
	return output;
}

void UChunkLocationData::addTreeSpawnPosition(const FVoxelObjectLocationData position) {
	TreesToSpawnSemaphore->Acquire();

	// If it exists, add the new tree position to the existing array
	if (treesSpawnPositions.Contains(position.ObjectWorldCoords)) {
		treesSpawnPositions[position.ObjectWorldCoords].Add(position);
	} else {
		// If not, create a new array with the new tree position
		treesSpawnPositions.Add(position.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({position}));
	}

	TreesToSpawnSemaphore->Release();
}

void UChunkLocationData::addGrassSpawnPosition(const FVoxelObjectLocationData position) {
	GrassToSpawnSemaphore->Acquire();

	// If it exists, add the new grass position to the existing array
	if (grassSpawnPositions.Contains(position.ObjectWorldCoords)) {
		grassSpawnPositions[position.ObjectWorldCoords].Add(position);
	} else {
		// If not, create a new array with the new grass position
		grassSpawnPositions.Add(position.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({position}));
	}

	GrassToSpawnSemaphore->Release();
}

void UChunkLocationData::addFlowerSpawnPosition(const FVoxelObjectLocationData position) {
	FlowersToSpawnSemaphore->Acquire();

	// If it exists, add the new flower position to the existing array
	if (flowersSpawnPositions.Contains(position.ObjectWorldCoords)) {
		flowersSpawnPositions[position.ObjectWorldCoords].Add(position);
	} else {
		// If not, create a new array with the new flower position
		flowersSpawnPositions.Add(position.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({position}));
	}

	FlowersToSpawnSemaphore->Release();
}

void UChunkLocationData::addNPCSpawnPosition(const TPair<FVoxelObjectLocationData, AnimalType> positionAndType) {
	NPCToSpawnSemaphore->Acquire();

	// If it exists, add the new NPC position to the existing array
	if (npcSpawnPositions.Contains(positionAndType.Key.ObjectWorldCoords)) {
		npcSpawnPositions[positionAndType.Key.ObjectWorldCoords].Add(positionAndType);
	} else {
		// If not, create a new array with the new NPC position
		npcSpawnPositions.Add(positionAndType.Key.ObjectWorldCoords, TArray<TPair<FVoxelObjectLocationData, AnimalType>>({positionAndType}));
	}

	NPCToSpawnSemaphore->Release();
}

void UChunkLocationData::addTreeSpawnPositions(const TArray<FVoxelObjectLocationData>& positions) {
	TreesToSpawnSemaphore->Acquire();
	for (const FVoxelObjectLocationData& pos : positions) {
		if (treesSpawnPositions.Contains(pos.ObjectWorldCoords)) {
			treesSpawnPositions[pos.ObjectWorldCoords].Add(pos);
		} else {
			treesSpawnPositions.Add(pos.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({pos}));
		}
	}
	TreesToSpawnSemaphore->Release();
}

void UChunkLocationData::addGrassSpawnPositions(const TArray<FVoxelObjectLocationData>& positions) {
	GrassToSpawnSemaphore->Acquire();
	for (const FVoxelObjectLocationData& pos : positions) {
		if (grassSpawnPositions.Contains(pos.ObjectWorldCoords)) {
			grassSpawnPositions[pos.ObjectWorldCoords].Add(pos);
		} else {
			grassSpawnPositions.Add(pos.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({pos}));
		}
	}
	GrassToSpawnSemaphore->Release();
}

void UChunkLocationData::addFlowerSpawnPositions(const TArray<FVoxelObjectLocationData>& positions) {
	FlowersToSpawnSemaphore->Acquire();
	for (const FVoxelObjectLocationData& pos : positions) {
		if (flowersSpawnPositions.Contains(pos.ObjectWorldCoords)) {
			flowersSpawnPositions[pos.ObjectWorldCoords].Add(pos);
		} else {
			flowersSpawnPositions.Add(pos.ObjectWorldCoords, TArray<FVoxelObjectLocationData>({pos}));
		}
	}
	FlowersToSpawnSemaphore->Release();
}

void UChunkLocationData::addNPCSpawnPositions(const TArray<TPair<FVoxelObjectLocationData, AnimalType>>& positionsAndTypes) {
	NPCToSpawnSemaphore->Acquire();
	for (const TPair<FVoxelObjectLocationData, AnimalType>& entry : positionsAndTypes) {
		const FIntPoint& key = entry.Key.ObjectWorldCoords;
		if (npcSpawnPositions.Contains(key)) {
			npcSpawnPositions[key].Add(entry);
		} else {
			npcSpawnPositions.Add(key, TArray<TPair<FVoxelObjectLocationData, AnimalType>>({entry}));
		}
	}
	NPCToSpawnSemaphore->Release();
}

void UChunkLocationData::RemoveTreeSpawnPosition(const FIntPoint& point) {
	TreesToSpawnSemaphore->Acquire();

	// Iterating over the map and removing the key and value
	for (TMap<FIntPoint, TArray<FVoxelObjectLocationData>>::TIterator SpawnPosition(treesSpawnPositions); SpawnPosition; ++SpawnPosition) {
		if (SpawnPosition.Key() == point) {
			SpawnPosition.RemoveCurrent();
			break;
		}
	}

	TreesToSpawnSemaphore->Release();
}

void UChunkLocationData::RemoveGrassSpawnPosition(const FIntPoint& point) {
	GrassToSpawnSemaphore->Acquire();

	// Iterating over the map and removing the key and value
	for (TMap<FIntPoint, TArray<FVoxelObjectLocationData>>::TIterator SpawnPosition(grassSpawnPositions); SpawnPosition; ++SpawnPosition) {
		if (SpawnPosition.Key() == point) {
			SpawnPosition.RemoveCurrent();
			break;
		}
	}

	GrassToSpawnSemaphore->Release();
}

void UChunkLocationData::RemoveFlowerSpawnPosition(const FIntPoint& point) {
	FlowersToSpawnSemaphore->Acquire();

	// Iterating over the map and removing the key and value
	for (TMap<FIntPoint, TArray<FVoxelObjectLocationData>>::TIterator SpawnPosition(flowersSpawnPositions); SpawnPosition; ++SpawnPosition) {
		if (SpawnPosition.Key() == point) {
			SpawnPosition.RemoveCurrent();
			break;
		}
	}

	FlowersToSpawnSemaphore->Release();
}

void UChunkLocationData::RemoveNPCSpawnPosition(const FIntPoint& point) {
	NPCToSpawnSemaphore->Acquire();

	// Iterating over the map and removing the key and value
	for (TMap<FIntPoint, TArray<TPair<FVoxelObjectLocationData, AnimalType>>>::TIterator SpawnPosition(npcSpawnPositions); SpawnPosition; ++SpawnPosition) {
		if (SpawnPosition.Key() == point) {
			SpawnPosition.RemoveCurrent();
			break;
		}
	}

	NPCToSpawnSemaphore->Release();
}

bool UChunkLocationData::AddUnspawnedTreeToDestroy(ATree* InTreeToDestroy) {
	return unspawnedTreesToDestroy.Enqueue(InTreeToDestroy);
}

bool UChunkLocationData::GetUnspawnedTreeToDestroy(ATree* InTreeToDestroy) {
	return unspawnedTreesToDestroy.Dequeue(InTreeToDestroy);
}

bool UChunkLocationData::AddUnspawnedGrassToDestroy(UProceduralMeshComponent* InGrassToDestroy) {
	return unspawnedGrassToDestroy.Enqueue(InGrassToDestroy);
}

bool UChunkLocationData::GetUnspawnedGrassToDestroy(UProceduralMeshComponent* InGrassToDestroy) {
	return unspawnedGrassToDestroy.Dequeue(InGrassToDestroy);
}

bool UChunkLocationData::AddUnspawnedFlowerToDestroy(UProceduralMeshComponent* InFlowerToDestroy) {
	return unspawnedFlowerToDestroy.Enqueue(InFlowerToDestroy);
}

bool UChunkLocationData::GetUnspawnedFlowerToDestroy(UProceduralMeshComponent* InFlowerToDestroy) {
	return unspawnedFlowerToDestroy.Dequeue(InFlowerToDestroy);
}

bool UChunkLocationData::AddUnspawnedNpcToDestroy(ABasicNPC* InNpcToDestroy) {
	return unspawnedNpcsToDestroy.Enqueue(InNpcToDestroy);
}

bool UChunkLocationData::GetUnspawnedNpcToDestroy(ABasicNPC* InNpcToDestroy) {
	return unspawnedNpcsToDestroy.Dequeue(InNpcToDestroy);
}

void UChunkLocationData::AddSurfaceVoxelPointsForChunk(const FIntPoint& chunkPosition, const TArray<int>& voxelPoints, const TArray<FVector2D>& avoidPoints) {
	SurfaceVoxelPointsSemaphore->Acquire();
	NavigationSurfaceChunks.Add(chunkPosition, FNavigationSurfaceChunk{voxelPoints, avoidPoints});
	SurfaceVoxelPointsSemaphore->Release();
}

void UChunkLocationData::RemoveSurfaceVoxelPointsForChunk(const FIntPoint& chunkPosition) {
	SurfaceVoxelPointsSemaphore->Acquire();
	NavigationSurfaceChunks.Remove(chunkPosition);
	SurfaceVoxelPointsSemaphore->Release();
}

TMap<FIntPoint, TArray<int>> UChunkLocationData::GetSurfaceVoxelPoints() {
	TMap<FIntPoint, TArray<int>> result;
	SurfaceVoxelPointsSemaphore->Acquire();
	for (const TPair<FIntPoint, FNavigationSurfaceChunk>& NavigationSurfacePair : NavigationSurfaceChunks) {
		result.Add(NavigationSurfacePair.Key, NavigationSurfacePair.Value.SurfaceVoxelPoints);
	}
	SurfaceVoxelPointsSemaphore->Release();
	return result;
}

bool UChunkLocationData::IsSurfacePointValid(const double& X, const double& Z) {
	// Adjust the coordinates to they're relative to the coordinates inside a single chunk
	const int relativeToChunkX = FMath::Floor(X / chunkSize);
	const int relativeToChunkZ = FMath::Floor(Z / chunkSize);

	const FIntPoint chunkCoords = FIntPoint(relativeToChunkX, relativeToChunkZ);

	TArray<FVector2D> avoidPoints;
	SurfaceVoxelPointsSemaphore->Acquire();
	if (const FNavigationSurfaceChunk* NavigationSurfaceChunk = NavigationSurfaceChunks.Find(chunkCoords)) {
		avoidPoints = NavigationSurfaceChunk->SurfaceAvoidPoints;
	}
	SurfaceVoxelPointsSemaphore->Release();

	// Check if any point in avoidPoints contains the points given
	const double modX = FMath::Max(((static_cast<int>(X) % chunkSize) + chunkSize) % chunkSize - 1, 0);
	const double modZ = FMath::Max(((static_cast<int>(Z) % chunkSize) + chunkSize) % chunkSize - 1, 0);

	bool isAvoidedPoint = avoidPoints.Contains(FVector2D(modX, modZ));
	if (isAvoidedPoint) {
		return false;
	}

	// Check if any point is occupied by a current NPC
	FIntPoint point = FIntPoint(
	    X * WTSR->UnrealScale + WTSR->HalfUnrealScale,
	    Z * WTSR->UnrealScale + WTSR->HalfUnrealScale
	); // Adjusting 2D points to Unreal's scale
	return !IsLocationOccupied(point);
}

bool UChunkLocationData::IsLocationOccupied(const FVector& currentPosition, const FVector& nextPosition, ABasicNPC* npcAtLocation) {
	FIntPoint nextPositionPoint = FIntPoint(nextPosition.X, nextPosition.Y);

	if (OccupiedVoxels.Contains(nextPositionPoint)) {
		return true;
	}

	// Remove the previous NPC location to allow other NPCs to move to that position
	FIntPoint previousPoint = FIntPoint(currentPosition.X, currentPosition.Y);
	if (OccupiedVoxels.Contains(previousPoint)) {
		OccupiedVoxels.Remove(previousPoint);
	}

	// Add the new position as occupied
	OccupiedVoxels.Add(nextPositionPoint, npcAtLocation);

	return false;
}

bool UChunkLocationData::IsLocationOccupied(const FIntPoint& position) {
	return OccupiedVoxels.Contains(position);
}

void UChunkLocationData::RemoveOccupiedVoxelPosition(const FVector& position) {
	FIntPoint point = FIntPoint(position.X, position.Y);

	// Remove the position from the OccupiedVoxels map if it exists
	if (OccupiedVoxels.Contains(point)) {
		OccupiedVoxels.Remove(point);
	}
}

void UChunkLocationData::AddOccupiedVoxelPosition(const FVector& position, ABasicNPC* npcAtLocation) {
	FIntPoint point = FIntPoint(position.X, position.Y);
	OccupiedVoxels.Add(point, npcAtLocation);
}

TMap<FIntPoint, ABasicNPC*> UChunkLocationData::GetOccupiedVoxels() const {
	return OccupiedVoxels;
}
