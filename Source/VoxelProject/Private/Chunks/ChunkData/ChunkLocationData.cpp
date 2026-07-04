#include "ChunkLocationData.h"

#include "..\TerrainSettings\WorldTerrainSettings.h"

namespace {
int64 GetChunkDistanceSquared(const FIntPoint& ChunkPosition, const FIntPoint& PriorityChunkPosition) {
	const int64 DistanceX = static_cast<int64>(ChunkPosition.X) - static_cast<int64>(PriorityChunkPosition.X);
	const int64 DistanceY = static_cast<int64>(ChunkPosition.Y) - static_cast<int64>(PriorityChunkPosition.Y);
	return DistanceX * DistanceX + DistanceY * DistanceY;
}

template <typename SpawnPositionType>
TArray<SpawnPositionType> TakeNearestSpawnPositions(
    TSet<FIntPoint>& InRangeChunkPositions,
    TMap<FIntPoint, TArray<SpawnPositionType>>& SpawnPositionsByChunk,
    const FIntPoint& PriorityChunkPosition
) {
	TArray<SpawnPositionType> Output;
	TArray<FIntPoint> EmptyChunkPositions;
	FIntPoint BestChunkPosition = FIntPoint::ZeroValue;
	int64 BestDistanceSquared = MAX_int64;
	bool bFoundSpawnPositions = false;

	for (const FIntPoint& ChunkPosition : InRangeChunkPositions) {
		const TArray<SpawnPositionType>* SpawnPositions = SpawnPositionsByChunk.Find(ChunkPosition);
		if (SpawnPositions == nullptr || SpawnPositions->IsEmpty()) {
			EmptyChunkPositions.Add(ChunkPosition);
			continue;
		}

		const int64 DistanceSquared = GetChunkDistanceSquared(ChunkPosition, PriorityChunkPosition);
		if (DistanceSquared < BestDistanceSquared) {
			BestChunkPosition = ChunkPosition;
			BestDistanceSquared = DistanceSquared;
			bFoundSpawnPositions = true;
		}
	}

	for (const FIntPoint& EmptyChunkPosition : EmptyChunkPositions) {
		InRangeChunkPositions.Remove(EmptyChunkPosition);
		SpawnPositionsByChunk.Remove(EmptyChunkPosition);
	}

	if (!bFoundSpawnPositions) {
		return Output;
	}

	if (TArray<SpawnPositionType>* SpawnPositions = SpawnPositionsByChunk.Find(BestChunkPosition)) {
		Output = MoveTemp(*SpawnPositions);
	}

	SpawnPositionsByChunk.Remove(BestChunkPosition);
	InRangeChunkPositions.Remove(BestChunkPosition);
	return Output;
}
}

UChunkLocationData::UChunkLocationData()
    : ChunkStreamingSemaphore(MakeUnique<FairSemaphore>(1)),
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
	ChunkStreamingSemaphore->Acquire();

	FVoxelObjectLocationData CandidateLocation;
	while (chunksToSpawnPositions.Dequeue(CandidateLocation)) {
		if (RequestedChunkSpawnPositions.Contains(CandidateLocation.ObjectWorldCoords)) {
			OutLocation = CandidateLocation;
			ChunkStreamingSemaphore->Release();
			return true;
		}
	}

	ChunkStreamingSemaphore->Release();
	return false;
}

bool UChunkLocationData::getChunkToDestroyPosition(FIntPoint& OutPosition) {
	ChunkStreamingSemaphore->Acquire();

	FIntPoint CandidatePosition = FIntPoint::ZeroValue;
	while (chunksToDestroyPositions.Dequeue(CandidatePosition)) {
		if (!RequestedChunkSpawnPositions.Contains(CandidatePosition)) {
			OutPosition = CandidatePosition;
			ChunkStreamingSemaphore->Release();
			return true;
		}
	}

	ChunkStreamingSemaphore->Release();
	return false;
}

bool UChunkLocationData::getComputedMeshDataAndLocationData(FVoxelObjectLocationData& locationData, FVoxelObjectMeshData& meshData, const FIntPoint& PriorityChunkPosition) {
	ChunkStreamingSemaphore->Acquire();
	MeshDataSemaphore->Acquire();

	int32 BestResultIndex = INDEX_NONE;
	int64 BestDistanceSquared = MAX_int64;

	for (int32 ResultIndex = ComputedMeshResults.Num() - 1; ResultIndex >= 0; --ResultIndex) {
		const FIntPoint& ChunkPosition = ComputedMeshResults[ResultIndex].LocationData.ObjectWorldCoords;
		if (!RequestedChunkSpawnPositions.Contains(ChunkPosition)) {
			ComputedMeshResults.RemoveAtSwap(ResultIndex, 1, EAllowShrinking::No);
			continue;
		}

		const int64 DistanceSquared = GetChunkDistanceSquared(ChunkPosition, PriorityChunkPosition);
		if (DistanceSquared < BestDistanceSquared) {
			BestResultIndex = ResultIndex;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestResultIndex == INDEX_NONE) {
		MeshDataSemaphore->Release();
		ChunkStreamingSemaphore->Release();
		return false;
	}

	FChunkMeshBuildResult MeshBuildResult = MoveTemp(ComputedMeshResults[BestResultIndex]);
	ComputedMeshResults.RemoveAtSwap(BestResultIndex, 1, EAllowShrinking::No);

	MeshDataSemaphore->Release();
	ChunkStreamingSemaphore->Release();

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

bool UChunkLocationData::IsChunkSpawnRequested(const FIntPoint& chunkPosition) const {
	ChunkStreamingSemaphore->Acquire();
	const bool bIsChunkSpawnRequested = RequestedChunkSpawnPositions.Contains(chunkPosition);
	ChunkStreamingSemaphore->Release();
	return bIsChunkSpawnRequested;
}

void UChunkLocationData::AddChunksToSpawnPosition(const FVoxelObjectLocationData position) {
	ChunkStreamingSemaphore->Acquire();

	if (!RequestedChunkSpawnPositions.Contains(position.ObjectWorldCoords)) {
		RequestedChunkSpawnPositions.Add(position.ObjectWorldCoords);
		chunksToSpawnPositions.Enqueue(position);
	}

	ChunkStreamingSemaphore->Release();
}

void UChunkLocationData::AddChunksToDestroyPosition(const FIntPoint& position) {
	ChunkStreamingSemaphore->Acquire();
	RequestedChunkSpawnPositions.Remove(position);
	chunksToDestroyPositions.Enqueue(position);
	ChunkStreamingSemaphore->Release();
}

void UChunkLocationData::AddMeshDataForPosition(const FVoxelObjectLocationData chunkLocationData, const FVoxelObjectMeshData meshData) {
	if (!IsChunkSpawnRequested(chunkLocationData.ObjectWorldCoords)) {
		return;
	}

	MeshDataSemaphore->Acquire();
	ComputedMeshResults.Add(FChunkMeshBuildResult{chunkLocationData, meshData});
	MeshDataSemaphore->Release();
}

void UChunkLocationData::AddTreeToDestroyPosition(const FIntPoint& treePosition) {
	treesToDestroy.Enqueue(treePosition);
}

bool UChunkLocationData::GetTreeToDestroyPosition(FIntPoint& treePosition) {
	return treesToDestroy.Dequeue(treePosition);
}

void UChunkLocationData::emptyPositionQueues() {
	ChunkStreamingSemaphore->Acquire();
	chunksToSpawnPositions.Empty();
	chunksToDestroyPositions.Empty();
	RequestedChunkSpawnPositions.Empty();
	ChunkStreamingSemaphore->Release();
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

	WTSR->CheckAndReturnGrassNotInRange(VegetationChunkSpawnPoints, GrassActorsToRemove);
	WTSR->CheckAndReturnFlowersNotInRange(VegetationChunkSpawnPoints, FlowerActorsToRemove);

	VegetationChunkSemaphore->Release();
}

void UChunkLocationData::CheckAndAddTreesNotInRange(TQueue<ATree*>* TreeActorsToRemove) {
	TreeChunkSemaphore->Acquire();

	WTSR->CheckAndReturnTreesNotInRange(TreeChunkSpawnPoints, TreeActorsToRemove);

	TreeChunkSemaphore->Release();
}

void UChunkLocationData::CheckAndAddNpcsNotInRange(TQueue<ABasicNPC*>* NpcActorsToRemove) {
	NpcChunkSemaphore->Acquire();

	WTSR->CheckAndReturnNpcsNotInRange(NpcChunkSpawnPoints, NpcActorsToRemove);

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

TArray<FVoxelObjectLocationData> UChunkLocationData::getTreeSpawnPositionsInRange(const FIntPoint& PriorityChunkPosition) {
	TArray<FVoxelObjectLocationData> output;
	TreeChunkSemaphore->Acquire();
	TreesToSpawnSemaphore->Acquire();

	output = TakeNearestSpawnPositions(treesInRangeSpawnPositions, treesSpawnPositions, PriorityChunkPosition);

	TreesToSpawnSemaphore->Release();
	TreeChunkSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getGrassSpawnPositionInRange(const FIntPoint& PriorityChunkPosition) {
	TArray<FVoxelObjectLocationData> output;
	VegetationChunkSemaphore->Acquire();
	GrassToSpawnSemaphore->Acquire();

	output = TakeNearestSpawnPositions(grassInRangeSpawnPositions, grassSpawnPositions, PriorityChunkPosition);

	GrassToSpawnSemaphore->Release();
	VegetationChunkSemaphore->Release();
	return output;
}

TArray<FVoxelObjectLocationData> UChunkLocationData::getFlowerSpawnPositionInRange(const FIntPoint& PriorityChunkPosition) {
	TArray<FVoxelObjectLocationData> output;
	VegetationChunkSemaphore->Acquire();
	FlowersToSpawnSemaphore->Acquire();

	output = TakeNearestSpawnPositions(flowersInRangeSpawnPositions, flowersSpawnPositions, PriorityChunkPosition);

	FlowersToSpawnSemaphore->Release();
	VegetationChunkSemaphore->Release();
	return output;
}

TArray<TPair<FVoxelObjectLocationData, AnimalType>> UChunkLocationData::getNPCSpawnPositionInRange(const FIntPoint& PriorityChunkPosition) {
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> output;
	NpcChunkSemaphore->Acquire();
	NPCToSpawnSemaphore->Acquire();

	output = TakeNearestSpawnPositions(npcInRangeSpawnPositions, npcSpawnPositions, PriorityChunkPosition);

	NPCToSpawnSemaphore->Release();
	NpcChunkSemaphore->Release();
	return output;
}

void UChunkLocationData::addTreeSpawnPosition(const FVoxelObjectLocationData position) {
	if (!IsChunkSpawnRequested(position.ObjectWorldCoords)) {
		return;
	}

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
	if (!IsChunkSpawnRequested(position.ObjectWorldCoords)) {
		return;
	}

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
	if (!IsChunkSpawnRequested(position.ObjectWorldCoords)) {
		return;
	}

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
	if (!IsChunkSpawnRequested(positionAndType.Key.ObjectWorldCoords)) {
		return;
	}

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
	if (positions.IsEmpty() || !IsChunkSpawnRequested(positions[0].ObjectWorldCoords)) {
		return;
	}

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
	if (positions.IsEmpty() || !IsChunkSpawnRequested(positions[0].ObjectWorldCoords)) {
		return;
	}

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
	if (positions.IsEmpty() || !IsChunkSpawnRequested(positions[0].ObjectWorldCoords)) {
		return;
	}

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
	if (positionsAndTypes.IsEmpty() || !IsChunkSpawnRequested(positionsAndTypes[0].Key.ObjectWorldCoords)) {
		return;
	}

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
	if (!IsChunkSpawnRequested(chunkPosition)) {
		return;
	}

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
