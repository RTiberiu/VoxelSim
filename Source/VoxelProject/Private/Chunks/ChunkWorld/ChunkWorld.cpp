// Fill out your copyright notice in the Description page of Project Settings.

#include "ChunkWorld.h"
#include "..\..\NPC\BasicNPC\BasicNPC.h"
#include "..\..\NPC\SettingsNPC\RelationshipSettingsNPC.h"
#include "..\ChunkData\ChunkLocationData.h"
#include "..\SingleChunk\BinaryChunk.h"
#include "..\TerrainSettings\WorldTerrainSettings.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Materials/MaterialInterface.h"
#include <Kismet/GameplayStatics.h>

#include "ProceduralMeshComponent.h"

#include <set>

namespace {
void SortLocationSpawnPositionsByDistance(TArray<FVoxelObjectLocationData>& SpawnPositions, const FVector& PlayerPosition) {
	// Sorting furthest first so nearby positions are removed from the end first
	SpawnPositions.Sort([&PlayerPosition](const FVoxelObjectLocationData& Left, const FVoxelObjectLocationData& Right) {
		return FVector::DistSquared(Left.ObjectPosition, PlayerPosition) > FVector::DistSquared(Right.ObjectPosition, PlayerPosition);
	});
}

void SortNpcSpawnPositionsByDistance(TArray<TPair<FVoxelObjectLocationData, AnimalType>>& SpawnPositions, const FVector& PlayerPosition) {
	// Sorting furthest first so nearby NPCs are removed from the end first
	SpawnPositions.Sort([&PlayerPosition](const TPair<FVoxelObjectLocationData, AnimalType>& Left, const TPair<FVoxelObjectLocationData, AnimalType>& Right) {
		return FVector::DistSquared(Left.Key.ObjectPosition, PlayerPosition) > FVector::DistSquared(Right.Key.ObjectPosition, PlayerPosition);
	});
}
} // namespace

// Sets default values
AChunkWorld::AChunkWorld() {
	// Set this actor to call Tick() every frame.  Yosu can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// Locking the tick at 60fps
	// PrimaryActorTick.TickInterval = 1.0f / 60.0f;

	isInitialWorldGenerated = false;

	// Initializing Chunk with the BinaryChunk class
	Chunk = ABinaryChunk::StaticClass();

	// Initializing Tree with the Tree class
	Tree = ATree::StaticClass();

	NPC = ABasicNPC::StaticClass();
}

void AChunkWorld::SetWorldTerrainSettings(UWorldTerrainSettings* InWorldTerrainSettings) {
	WorldTerrainSettingsRef = InWorldTerrainSettings;
}

void AChunkWorld::SetChunkLocationData(UChunkLocationData* InChunkLocationData) {
	ChunkLocationDataRef = InChunkLocationData;
}

void AChunkWorld::SetPerlinNoiseSettings(APerlinNoiseSettings* InPerlinNoiseSettings) {
	PerlinNoiseSettingsRef = InPerlinNoiseSettings;

	// Create all the instances of perlin noise and set their settings
	WTSR->SetPerlinNoiseSettings(InPerlinNoiseSettings);
}

void AChunkWorld::SetAnimationSettingsNpc(UAnimationSettingsNPC* InAnimationSettingsRef) {
	AnimationSettingsRef = InAnimationSettingsRef;
}

void AChunkWorld::SetStatsVoxelsMeshNPC(UStatsVoxelsMeshNPC* InStatsVoxelsMeshNPC) {
	StatsVoxelsMeshNPCRef = InStatsVoxelsMeshNPC;
}

void AChunkWorld::InitializePathfindingManager() {
	// Initialize thread pool for the NPC pathfinding
	const int PathfindingThreads = 3;
	PathfindingManager = MakeUnique<PathfindingThreadManager>(WTSR, CLDR, PathfindingThreads);
}

void AChunkWorld::printExecutionTime(Time& start, Time& end, const char* functionName) {
	std::chrono::duration<double, std::milli> duration = end - start;
	UE_LOG(LogTemp, Warning, TEXT("%s() took %d seconds, %d milliseconds to execute."), *FString(functionName), static_cast<int>((duration.count() / 1000)) % 60, static_cast<int>(fmod(duration.count(), 1000)));
}

void AChunkWorld::spawnInitialWorld() {
	int spawnedChunks{0};

	// Queueing the centre chunk first so the world has a guaranteed anchor
	FIntPoint PlayerStartCoords = FIntPoint(0, 0);
	FVector ChunkPosition = FVector(0, 0, 0);
	FIntPoint ChunkWorldCoords = FIntPoint(0, 0);
	CLDR->AddChunksToSpawnPosition(FVoxelObjectLocationData(ChunkPosition, ChunkWorldCoords));
	CLDR->AddVegetationChunkSpawnPosition(ChunkWorldCoords);
	CLDR->AddTreeChunkSpawnPosition(ChunkWorldCoords);
	CLDR->AddNpcChunkSpawnPosition(ChunkWorldCoords);

	// Walking square rings around the origin so nearer chunks enter the queue first
	std::set<std::pair<int, int>> avoidPosition = {{0, 0}};
	int currentSpiralRing = 1;
	int maxSpiralRings = WTSR->DrawDistance;
	int vegetationMax = WTSR->VegetationDrawDistance;
	int treeMax = WTSR->TreeDrawDistance;
	int npcMax = WTSR->NpcDrawDistance;

	while (currentSpiralRing <= maxSpiralRings) {
		for (int x = -currentSpiralRing; x < currentSpiralRing; x++) {
			for (int z = -currentSpiralRing; z < currentSpiralRing; z++) {
				std::pair<int, int> currentPair = {x, z};

				if (avoidPosition.find(currentPair) != avoidPosition.end()) {
					continue;
				}

				ChunkPosition = FVector(x * WTSR->chunkSize * WTSR->UnrealScale, z * WTSR->chunkSize * WTSR->UnrealScale, 0);
				ChunkWorldCoords = FIntPoint(x, z);

				CLDR->AddChunksToSpawnPosition(FVoxelObjectLocationData(ChunkPosition, ChunkWorldCoords));

				int ringDistance = FMath::Max(FMath::Abs(x), FMath::Abs(z));

				// Tracking cheaper object ranges separately from terrain draw distance
				if (ringDistance < vegetationMax) {
					CLDR->AddVegetationChunkSpawnPosition(ChunkWorldCoords);
				}

				if (ringDistance < treeMax) {
					CLDR->AddTreeChunkSpawnPosition(ChunkWorldCoords);
				}

				if (ringDistance < npcMax) {
					CLDR->AddNpcChunkSpawnPosition(ChunkWorldCoords);
				}

				avoidPosition.insert(currentPair);
			}
		}

		currentSpiralRing++;
	}
}

void AChunkWorld::UseTestingConfigurations(ConfigToRun configToRun) {
	if (SpawnedConfigOnce) {
		return;
	}

	TestConfigParameters ConfigData;

	switch (configToRun) {
	case NotificationAttackNpc:
		ConfigData = TestingConfig::GetNotificationAttackNpcTest();
		break;

	case NotificationAttackFoodSource:
		ConfigData = TestingConfig::GetNotificationAttackFoodTest();
		break;

	default:
		return;
	}

	// Spawn all NPCs
	for (int Index = WTSR->NPCCount; Index < ConfigData.NpcPositions.Num(); Index++) {
		SpawnNPC(ConfigData.NpcPositions[Index]);
		WTSR->NPCCount++;
	}

	// Spawn all Grass
	if (WTSR->GrassCount == 0) {
		for (int Index = WTSR->GrassCount; Index < ConfigData.Grass.Num(); Index++) {
			SpawnGrass(ConfigData.Grass[Index]);
			WTSR->GrassCount++;
		}
	}

	// Spawn all Flowers
	if (WTSR->FlowerCount == 0) {
		for (int Index = WTSR->FlowerCount; Index < ConfigData.Flowers.Num(); Index++) {
			SpawnFlower(ConfigData.Flowers[Index]);
			WTSR->FlowerCount++;
		}
	}

	SpawnedConfigOnce = true;
}

void AChunkWorld::generateTreeMeshVariations() {
	Time start = std::chrono::high_resolution_clock::now();

	UTreeMeshGenerator* TreeMeshGenerator = NewObject<UTreeMeshGenerator>();
	TreeMeshGenerator->SetWorldTerrainSettings(WTSR);

	for (int treeIndex = 0; treeIndex < WTSR->TreeVariations; treeIndex++) {
		const FColor trunkColor = WTSR->TreeTrunkColorArray[treeIndex % WTSR->TreeTrunkColorArray.Num()];
		const FColor crownColor = WTSR->TreeCrownColorArray[treeIndex % WTSR->TreeCrownColorArray.Num()];

		FVoxelObjectMeshData treeMeshData = TreeMeshGenerator->GetTreeMeshData(trunkColor, crownColor);
		WTSR->AddTreeMeshData(treeMeshData);
	}

	Time end = std::chrono::high_resolution_clock::now();
	printExecutionTime(start, end, std::format("Generated {} tree variations.", WTSR->TreeVariations).c_str());
}

void AChunkWorld::generateGrassMeshVariations() {
	Time start = std::chrono::high_resolution_clock::now();

	UGrassMeshGenerator* GrassMeshGenerator = NewObject<UGrassMeshGenerator>();
	GrassMeshGenerator->SetWorldTerrainSettings(WTSR);

	for (int grassIndex = 0; grassIndex < WTSR->GrassVariations; grassIndex++) {
		const FColor grassColor = WTSR->GrassBladesColorArray[grassIndex % WTSR->GrassBladesColorArray.Num()];
		FVoxelObjectMeshData grassMeshData = GrassMeshGenerator->GetGrassMeshData(grassColor);
		WTSR->AddGrassMeshData(grassMeshData);
	}

	Time end = std::chrono::high_resolution_clock::now();
	printExecutionTime(start, end, std::format("Generated {} grass variations.", WTSR->GrassVariations).c_str());
}

void AChunkWorld::generateFlowerMeshVariations() {
	Time start = std::chrono::high_resolution_clock::now();

	UFlowerMeshGenerator* FlowerMeshGenerator = NewObject<UFlowerMeshGenerator>();
	FlowerMeshGenerator->SetWorldTerrainSettings(WTSR);

	for (int flowerIndex = 0; flowerIndex < WTSR->FlowerVariations; flowerIndex++) {
		const FColor stemColor = WTSR->FlowerStemColorArray[flowerIndex % WTSR->FlowerStemColorArray.Num()];
		const FColor petalColor = WTSR->FlowerPetalColorArray[flowerIndex % WTSR->FlowerPetalColorArray.Num()];

		FVoxelObjectMeshData flowerMeshData = FlowerMeshGenerator->GetFlowerMeshData(stemColor, petalColor);
		WTSR->AddFlowerMeshData(flowerMeshData);
	}

	Time end = std::chrono::high_resolution_clock::now();
	printExecutionTime(start, end, std::format("Generated {} flower variations.", WTSR->FlowerVariations).c_str());
}

// Perform any actions after generating the new chunks
void AChunkWorld::onNewTerrainGenerated() {
	// Spawn the tree TODO Continue from here
}

void AChunkWorld::destroyCurrentWorldChunks() {
	bool isWorldEmpty = false;

	// Clearing spawned chunks before rebuilding the queued world from updated settings
	while (!isWorldEmpty) {
		AActor* chunkToRemove = WTSR->GetNextChunkFromMap();
		if (chunkToRemove) {
			chunkToRemove->Destroy();
		} else {
			// Empty indices for chunks to spawn and destroy
			CLDR->emptyPositionQueues();

			isWorldEmpty = true;
		}
	}
}

void AChunkWorld::SpawnTrees(FVoxelObjectLocationData LocationData, FVector PlayerPosition) {
	// Spawn the chunk actor deferred
	ATree* SpawnedTreeActor = GetWorld()->SpawnActorDeferred<ATree>(Tree, FTransform(FRotator::ZeroRotator, LocationData.ObjectPosition), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (SpawnedTreeActor) {
		// Add references to BinaryChunk and pass the computed mesh data
		SpawnedTreeActor->SetWorldTerrainSettings(WTSR);
		SpawnedTreeActor->SetTreeMeshData(WTSR->GetRandomTreeMeshData());
		SpawnedTreeActor->SetTreeWorldLocation(LocationData.ObjectWorldCoords);
		SpawnedTreeActor->SetTreeChunkRelativeLocation(LocationData.ObjectPosition);

		// Define the boundaries for the collision check
		float minX = PlayerPosition.X - WTSR->VegetationCollisionDistance;
		float maxX = PlayerPosition.X + WTSR->VegetationCollisionDistance;
		float minY = PlayerPosition.Y - WTSR->VegetationCollisionDistance;
		float maxY = PlayerPosition.Y + WTSR->VegetationCollisionDistance;

		// Check if the player is within the collision boundaries
		bool withinCollisionDistance = (LocationData.ObjectPosition.X >= minX && LocationData.ObjectPosition.X <= maxX) &&
		                               (LocationData.ObjectPosition.Y >= minY && LocationData.ObjectPosition.Y <= maxY);

		if (withinCollisionDistance) {
			SpawnedTreeActor->SetTreeCollision(true);
		}

		// Finish spawning the chunk actor
		UGameplayStatics::FinishSpawningActor(SpawnedTreeActor, FTransform(FRotator::ZeroRotator, LocationData.ObjectPosition));

		// TODO Add the tree actor to a map so I can update the collision and remove it later on
		WTSR->AddSpawnedTrees(LocationData.ObjectWorldCoords, SpawnedTreeActor);
	} else {
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn Tree Actor!"));
	}
}

void AChunkWorld::SpawnGrass(FVoxelObjectLocationData LocationData) {
	if (VoxelBasicMaterial == nullptr) {
		CacheVoxelBasicMaterial();
	}

	UCustomProceduralMeshComponent* Mesh = NewObject<UCustomProceduralMeshComponent>(this);
	Mesh->RegisterComponent();
	Mesh->SetCastShadow(WTSR->GrassShadow);

	FVoxelObjectMeshData* MeshData = WTSR->GetRandomGrassMeshData();
	Mesh->CreateMeshSection(0, MeshData->Vertices, MeshData->Triangles, MeshData->Normals, MeshData->UV0, MeshData->Colors, TArray<FProcMeshTangent>(), true);

	// Set up simplified collision
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);

	// Mesh->ComponentTags.Add(TEXT("Grass"));

	// Setting custom data
	Mesh->MeshType = MeshType::Grass;
	Mesh->ObjectWorldCoords = LocationData.ObjectWorldCoords;

	if (VoxelBasicMaterial != nullptr) {
		Mesh->SetMaterial(0, VoxelBasicMaterial);
	}

	Mesh->SetWorldLocation(LocationData.ObjectPosition);
	Mesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

	// Adding the grass object to a map so I can remove it later on
	WTSR->AddSpawnedGrass(LocationData.ObjectWorldCoords, Mesh);
}

void AChunkWorld::SpawnFlower(FVoxelObjectLocationData LocationData) {
	if (VoxelBasicMaterial == nullptr) {
		CacheVoxelBasicMaterial();
	}

	UCustomProceduralMeshComponent* Mesh = NewObject<UCustomProceduralMeshComponent>(this);
	Mesh->RegisterComponent();
	Mesh->SetCastShadow(WTSR->FlowerShadow);

	FVoxelObjectMeshData* MeshData = WTSR->GetRandomFlowerMeshData();
	Mesh->CreateMeshSection(0, MeshData->Vertices, MeshData->Triangles, MeshData->Normals, MeshData->UV0, MeshData->Colors, TArray<FProcMeshTangent>(), true);

	// Set up simplified collision
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);

	// Mesh->ComponentTags.Add(TEXT("Flower"));

	// Setting custom data
	Mesh->MeshType = MeshType::Flower;
	Mesh->ObjectWorldCoords = LocationData.ObjectWorldCoords;

	if (VoxelBasicMaterial != nullptr) {
		Mesh->SetMaterial(0, VoxelBasicMaterial);
	}

	Mesh->SetWorldLocation(LocationData.ObjectPosition);
	Mesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

	// Adding the tree object to a map so I can remove it later on
	WTSR->AddSpawnedFlower(LocationData.ObjectWorldCoords, Mesh);
}

void AChunkWorld::CacheVoxelBasicMaterial() {
	if (VoxelBasicMaterial != nullptr) {
		return;
	}

	VoxelBasicMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VoxelBasicMaterial.VoxelBasicMaterial"));
	if (VoxelBasicMaterial == nullptr) {
		UE_LOG(LogTemp, Warning, TEXT("VoxelBasicMaterial could not be loaded for vegetation meshes."));
	}
}

void AChunkWorld::SpawnNPC(TPair<FVoxelObjectLocationData, AnimalType> LocationAndType) {
	// Spawn the NPC actor deferred
	ABasicNPC* SpawnedNPCActor = GetWorld()->SpawnActorDeferred<ABasicNPC>(NPC, FTransform(FRotator::ZeroRotator, LocationAndType.Key.ObjectPosition), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (SpawnedNPCActor) {
		// Add references to the NPC and pass the pathfinding manager for making requests
		SpawnedNPCActor->SetWorldTerrainSettings(WTSR);
		SpawnedNPCActor->SetChunkLocationData(CLDR);
		SpawnedNPCActor->SetAnimationSettingsNPC(AnimS);
		SpawnedNPCActor->SetPathfindingManager(PathfindingManager.Get());
		SpawnedNPCActor->SetNPCWorldLocation(LocationAndType.Key.ObjectWorldCoords);
		SpawnedNPCActor->InitializeBrain(LocationAndType.Value);
		SpawnedNPCActor->SetStatsVoxelsMeshNPC(SVMNpc);

		// Finish spawning the chunk actor
		UGameplayStatics::FinishSpawningActor(SpawnedNPCActor, FTransform(FRotator::ZeroRotator, LocationAndType.Key.ObjectPosition));

		// This occupies the current voxel where the NPC is spawned (used for collision avoidance during pathfinding)
		CLDR->AddOccupiedVoxelPosition(LocationAndType.Key.ObjectPosition, SpawnedNPCActor);

		// Add the NPC actor to a map so I can update the collision and remove it later on
		WTSR->AddSpawnedNpc(LocationAndType.Key.ObjectWorldCoords, SpawnedNPCActor);
	} else {
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn NPC Actor!"));
	}
}

// Removing pending vegetation and NPC spawn work for a chunk being destroyed
// Queueing live actors so destruction can be spread across multiple frames in Tick
void AChunkWorld::RemoveVegetationSpawnPointsAndActors(const FIntPoint& destroyPosition) {
	// Remove remaining trees to spawn position at current chunk destroyed
	CLDR->RemoveTreeSpawnPosition(destroyPosition);

	// Remove trees to spawn in the ChunkWorld cache
	TreePositionsToSpawn.RemoveAll([&](const FVoxelObjectLocationData& Item) {
		return Item.ObjectWorldCoords == destroyPosition;
	});

	TArray<ATree*> TreesToRemove = WTSR->GetAndRemoveTreeFromMap(destroyPosition);
	for (ATree* TreeToRemove : TreesToRemove) {
		TreeActorsToRemove.Enqueue(TreeToRemove);
	}

	// Remove remaining grass to spawn position at current chunk destroyed
	CLDR->RemoveGrassSpawnPosition(destroyPosition);

	// Remove grass to spawn in the ChunkWorld cache
	GrassPositionsToSpawn.RemoveAll([&](const FVoxelObjectLocationData& Item) {
		return Item.ObjectWorldCoords == destroyPosition;
	});

	TArray<UCustomProceduralMeshComponent*> GrassToRemove = WTSR->GetAndRemoveGrassFromMap(destroyPosition);
	for (UCustomProceduralMeshComponent* GrassToRemoveComponent : GrassToRemove) {
		GrassActorsToRemove.Enqueue(GrassToRemoveComponent);
	}

	// Remove remaining flower to spawn position at current chunk destroyed
	CLDR->RemoveFlowerSpawnPosition(destroyPosition);

	// Remove flower to spawn in the ChunkWorld cache
	FlowerPositionsToSpawn.RemoveAll([&](const FVoxelObjectLocationData& Item) {
		return Item.ObjectWorldCoords == destroyPosition;
	});

	TArray<UCustomProceduralMeshComponent*> FlowersToRemove = WTSR->GetAndRemoveFlowerFromMap(destroyPosition);
	for (UCustomProceduralMeshComponent* FlowerToRemoveComponent : FlowersToRemove) {
		FlowerActorsToRemove.Enqueue(FlowerToRemoveComponent);
	}

	// Remove remaining NPC to spawn position at current chunk destroyed
	CLDR->RemoveNPCSpawnPosition(destroyPosition);

	// Remove NPCs to spawn in the ChunkWorld cache
	NPCPositionsToSpawn.RemoveAll([&](const TPair<FVoxelObjectLocationData, AnimalType>& Item) {
		return Item.Key.ObjectWorldCoords == destroyPosition;
	});

	TArray<ABasicNPC*> NpcsToRemove = WTSR->GetAndRemoveNpcFromMap(destroyPosition);
	for (ABasicNPC* NpcToRemove : NpcsToRemove) {
		NpcActorsToRemove.Enqueue(NpcToRemove);
	}
}

void AChunkWorld::DestroyTreeActors() {
	// Limiting removals per frame to avoid destruction spikes when chunks stream out
	int removedTreeCounter = 0;
	while (!TreeActorsToRemove.IsEmpty() && removedTreeCounter < treesToRemovePerFrame) {

		ATree* treeToRemove = nullptr;
		if (TreeActorsToRemove.Dequeue(treeToRemove)) {
			if (IsValid(treeToRemove)) {
				treeToRemove->Destroy();
				WTSR->TreeCount--;
			}
		}
		removedTreeCounter++;
	}
}

void AChunkWorld::DestroyGrassActors() {
	// Limiting component destruction because vegetation can leave range in large batches
	int removedGrassCounter = 0;
	while (!GrassActorsToRemove.IsEmpty() && removedGrassCounter < grassToRemovePerFrame) {

		UCustomProceduralMeshComponent* grassToRemove = nullptr;
		GrassActorsToRemove.Peek(grassToRemove);

		if (!IsValid(grassToRemove)) {
			GrassActorsToRemove.Dequeue(grassToRemove);
			removedGrassCounter++;
			continue;
		}

		GrassActorsToRemove.Dequeue(grassToRemove);
		grassToRemove->DestroyComponent();
		WTSR->GrassCount--;
		removedGrassCounter++;
	}
}

void AChunkWorld::DestroyFlowerActors() {
	// Limiting component destruction because vegetation can leave range in large batches
	int removedFlowerCounter = 0;
	while (!FlowerActorsToRemove.IsEmpty() && removedFlowerCounter < flowerToRemovePerFrame) {

		UCustomProceduralMeshComponent* flowerToRemove = nullptr;
		FlowerActorsToRemove.Peek(flowerToRemove);

		if (!IsValid(flowerToRemove)) {
			FlowerActorsToRemove.Dequeue(flowerToRemove);
			removedFlowerCounter++;
			continue;
		}

		FlowerActorsToRemove.Dequeue(flowerToRemove);
		flowerToRemove->DestroyComponent();
		WTSR->FlowerCount--;
		removedFlowerCounter++;
	}
}

void AChunkWorld::DestroyNpcActors() {
	// Limiting NPC destruction to keep streaming work spread across frames
	int removedNpcCounter = 0;
	while (!NpcActorsToRemove.IsEmpty() && removedNpcCounter < npcToRemovePerFrame) {

		ABasicNPC* npcToRemove = nullptr;
		NpcActorsToRemove.Peek(npcToRemove);

		if (!IsValid(npcToRemove)) {
			NpcActorsToRemove.Dequeue(npcToRemove);
			removedNpcCounter++;
			continue;
		}

		NpcActorsToRemove.Dequeue(npcToRemove);
		npcToRemove->Destroy();
		WTSR->NPCCount--;
		removedNpcCounter++;
	}
}

void AChunkWorld::SpawnMultipleGrassObjects(const FVector& PlayerPosition) {
	// Gathering spawn positions currently in range before trimming stale cached entries
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	TArray<FVoxelObjectLocationData> grassSpawnPositions = CLDR->getGrassSpawnPositionInRange(PlayerChunkCoords);
	GrassPositionsToSpawn.Append(grassSpawnPositions);
	TrimCachedSpawnPositions();
	SortLocationSpawnPositionsByDistance(GrassPositionsToSpawn, PlayerPosition);

	// Spawn a few trees in the current frames
	int spawnedGrassCounter = 0;
	while (!GrassPositionsToSpawn.IsEmpty()) {
		if (spawnedGrassCounter >= grassToSpawnPerFrame) {
			return;
		}

		const FVoxelObjectLocationData GrassPositionToSpawn = GrassPositionsToSpawn.Pop(EAllowShrinking::No);

		// Check if the grass position is still in range, otherwise discard it
		bool isGrassStillInRange = VegetationChunkSpawnPoints.Contains(GrassPositionToSpawn.ObjectWorldCoords);
		if (isGrassStillInRange) {
			SpawnGrass(GrassPositionToSpawn);
			WTSR->GrassCount++;
			spawnedGrassCounter++;
		}

		// Printing the grass count every 50
		/*if (WTSR->GrassCount % 1000 == 0) {
		    UE_LOG(LogTemp, Log, TEXT("Grass count: %d"), WTSR->GrassCount);
		}*/
	}
}

void AChunkWorld::SpawnMultipleFlowerObjects(const FVector& PlayerPosition) {

	// Gathering spawn positions currently in range before trimming stale cached entries
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	TArray<FVoxelObjectLocationData> flowerSpawnPositions = CLDR->getFlowerSpawnPositionInRange(PlayerChunkCoords);
	FlowerPositionsToSpawn.Append(flowerSpawnPositions);
	TrimCachedSpawnPositions();
	SortLocationSpawnPositionsByDistance(FlowerPositionsToSpawn, PlayerPosition);

	// Spawn a few flowers in the current frame
	int spawnedFlowerCounter = 0;
	while (!FlowerPositionsToSpawn.IsEmpty()) {
		if (spawnedFlowerCounter >= flowerToSpawnPerFrame) {
			return;
		}

		const FVoxelObjectLocationData FlowerPositionToSpawn = FlowerPositionsToSpawn.Pop(EAllowShrinking::No);

		// Check if the flower position is still in range, otherwise discard it
		bool isFlowerStillInRange = VegetationChunkSpawnPoints.Contains(FlowerPositionToSpawn.ObjectWorldCoords);
		if (isFlowerStillInRange) {
			SpawnFlower(FlowerPositionToSpawn);
			WTSR->FlowerCount++;
			spawnedFlowerCounter++;
		}

		// Printing the flower count every 50
		/*if (WTSR->FlowerCount % 50 == 0) {
		    UE_LOG(LogTemp, Log, TEXT("Flower count: %d"), WTSR->FlowerCount);
		}*/
	}
}

void AChunkWorld::SpawnMultipleNpcObjects(const FVector& PlayerPosition) {
	// Gathering NPC spawn positions currently in range before trimming stale cached entries
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	TArray<TPair<FVoxelObjectLocationData, AnimalType>> NPCSpawnPositions = CLDR->getNPCSpawnPositionInRange(PlayerChunkCoords);
	NPCPositionsToSpawn.Append(NPCSpawnPositions);
	TrimCachedSpawnPositions();
	SortNpcSpawnPositionsByDistance(NPCPositionsToSpawn, PlayerPosition);

	// Spawn a few flowers in the current frame
	int spawnedNPCCounter = 0;
	while (!NPCPositionsToSpawn.IsEmpty()) {
		if (spawnedNPCCounter >= npcToSpawnPerFrame) {
			break;
		}

		const TPair<FVoxelObjectLocationData, AnimalType> NpcPositionToSpawn = NPCPositionsToSpawn.Pop(EAllowShrinking::No);

		bool isNpcStillInRange = NpcChunkSpawnPoints.Contains(NpcPositionToSpawn.Key.ObjectWorldCoords);
		if (isNpcStillInRange) {
			SpawnNPC(NpcPositionToSpawn);
			WTSR->NPCCount++;
			spawnedNPCCounter++;
		}

		// Printing the NPC count every 10
		// if (WTSR->NPCCount % 10 == 0) {
		//	UE_LOG(LogTemp, Log, TEXT("NPC count: %d"), WTSR->NPCCount);
		//}
	}
}

void AChunkWorld::SpawnMultipleTreeObjects(const FVector& PlayerPosition) {
	// Gathering tree spawn positions currently in range before trimming stale cached entries
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	TArray<FVoxelObjectLocationData> treeSpawnPositions = CLDR->getTreeSpawnPositionsInRange(PlayerChunkCoords);
	TreePositionsToSpawn.Append(treeSpawnPositions);
	TrimCachedSpawnPositions();
	SortLocationSpawnPositionsByDistance(TreePositionsToSpawn, PlayerPosition);

	int spawnedTreeCounter = 0;
	while (!TreePositionsToSpawn.IsEmpty()) {
		if (spawnedTreeCounter >= treesToSpawnPerFrame) {
			spawnedTreesThisFrame = true;
			break;
		}

		const FVoxelObjectLocationData TreePositionToSpawn = TreePositionsToSpawn.Pop(EAllowShrinking::No);

		// Check if the tree position is still in range, otherwise discard it
		bool isTreeStillInRange = TreeChunkSpawnPoints.Contains(TreePositionToSpawn.ObjectWorldCoords);
		if (isTreeStillInRange) {
			SpawnTrees(TreePositionToSpawn, PlayerPosition);
			WTSR->TreeCount++;
			spawnedTreeCounter++;

			// Printing the tree count every 50
			if (WTSR->TreeCount % 1000 == 0) {
				UE_LOG(LogTemp, Log, TEXT("Tree count: %d"), WTSR->TreeCount);
			}
		}
	}
}

void AChunkWorld::TrimCachedSpawnPositions() {
	// Dropping cached positions that are no longer inside the active spawn ranges
	TreePositionsToSpawn.RemoveAllSwap([this](const FVoxelObjectLocationData& Item) {
		return !TreeChunkSpawnPoints.Contains(Item.ObjectWorldCoords);
	},
	                                   EAllowShrinking::No);

	GrassPositionsToSpawn.RemoveAllSwap([this](const FVoxelObjectLocationData& Item) {
		return !VegetationChunkSpawnPoints.Contains(Item.ObjectWorldCoords);
	},
	                                    EAllowShrinking::No);

	FlowerPositionsToSpawn.RemoveAllSwap([this](const FVoxelObjectLocationData& Item) {
		return !VegetationChunkSpawnPoints.Contains(Item.ObjectWorldCoords);
	},
	                                     EAllowShrinking::No);

	NPCPositionsToSpawn.RemoveAllSwap([this](const TPair<FVoxelObjectLocationData, AnimalType>& Item) {
		return !NpcChunkSpawnPoints.Contains(Item.Key.ObjectWorldCoords);
	},
	                                  EAllowShrinking::No);
}

void AChunkWorld::UpdateChunksCollision() {
	// Enabling and disabling collision for chunks
	ABinaryChunk* removeCollisionChunk = WTSR->GetChunkToRemoveCollision();
	ABinaryChunk* enableCollisionChunk = WTSR->GetChunkToEnableCollision();

	if (IsValid(removeCollisionChunk) && removeCollisionChunk->IsActorInitialized()) {
		removeCollisionChunk->UpdateCollision(false);
	}

	if (IsValid(enableCollisionChunk) && enableCollisionChunk->IsActorInitialized()) {
		enableCollisionChunk->UpdateCollision(true);
	}
}

void AChunkWorld::UpdateTreesCollision() {
	// Enabling and disabling collision for trees
	ATree* removeCollisionTree = WTSR->GetTreeToRemoveCollision();
	ATree* enableCollisionTree = WTSR->GetTreeToEnableCollision();

	if (IsValid(removeCollisionTree) && removeCollisionTree->IsActorInitialized()) {
		removeCollisionTree->UpdateCollision(false);
	}

	if (IsValid(enableCollisionTree) && enableCollisionTree->IsActorInitialized()) {
		enableCollisionTree->UpdateCollision(true);
	}
}

void AChunkWorld::SpawnSingleChunk(const FVector& PlayerPosition) {
	if (!CLDR->isMeshWaitingToBeSpawned()) {
		return;
	}

	// Marking this frame as chunk heavy so later vegetation spawning can yield
	spawnedChunksThisFrame = true;

	// Get the location data and the computed mesh data for the chunk
	FVoxelObjectLocationData waitingMeshLocationData;
	FVoxelObjectMeshData waitingMeshData;
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	const bool bHasComputedMeshData = CLDR->getComputedMeshDataAndLocationData(waitingMeshLocationData, waitingMeshData, PlayerChunkCoords);
	if (!bHasComputedMeshData) {
		return;
	}

	if (!CLDR->IsChunkSpawnRequested(waitingMeshLocationData.ObjectWorldCoords)) {
		return;
	}

	Time start = std::chrono::high_resolution_clock::now();

	// Spawn the chunk actor deferred
	ABinaryChunk* SpawnedChunkActor = GetWorld()->SpawnActorDeferred<ABinaryChunk>(Chunk, FTransform(FRotator::ZeroRotator, waitingMeshLocationData.ObjectPosition), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (SpawnedChunkActor) {
		// Add references to BinaryChunk and pass the computed mesh data
		SpawnedChunkActor->SetWorldTerrainSettings(WTSR);
		SpawnedChunkActor->SetComputedMeshData(waitingMeshData);
		SpawnedChunkActor->SetChunkLocation(waitingMeshLocationData.ObjectWorldCoords);

		// Define the boundaries for the collision check
		float minX = PlayerPosition.X - WTSR->CollisionDistance;
		float maxX = PlayerPosition.X + WTSR->CollisionDistance;
		float minY = PlayerPosition.Y - WTSR->CollisionDistance;
		float maxY = PlayerPosition.Y + WTSR->CollisionDistance;

		// Check if the player is within the collision boundaries
		bool withinCollisionDistance = (waitingMeshLocationData.ObjectPosition.X >= minX && waitingMeshLocationData.ObjectPosition.X <= maxX) &&
		                               (waitingMeshLocationData.ObjectPosition.Y >= minY && waitingMeshLocationData.ObjectPosition.Y <= maxY);

		if (withinCollisionDistance) {
			SpawnedChunkActor->SetChunkCollision(true);
		}

		// Finish spawning the chunk actor
		UGameplayStatics::FinishSpawningActor(SpawnedChunkActor, FTransform(FRotator::ZeroRotator, waitingMeshLocationData.ObjectPosition));

		WTSR->AddChunkToMap(waitingMeshLocationData.ObjectWorldCoords, SpawnedChunkActor);
	} else {
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn Chunk Actor!"));
	}

	Time end = std::chrono::high_resolution_clock::now();
	calculateAverageChunkSpawnTime(start, end);
}

void AChunkWorld::DestroySingleChunk() {
	FIntPoint chunkToDestroyPosition{};
	bool doesDestroyPositionExist = CLDR->getChunkToDestroyPosition(chunkToDestroyPosition);

	// Skipping when no streamed out chunk is ready for destruction
	if (!doesDestroyPositionExist) {
		return;
	}

	AActor* chunkToRemove = WTSR->GetAndRemoveChunkFromMap(chunkToDestroyPosition);
	if (IsValid(chunkToRemove)) {
		chunkToRemove->Destroy();

		// Removing remaining object work tied to this chunk before pathfinding data is cleared
		RemoveVegetationSpawnPointsAndActors(chunkToDestroyPosition);

		// Remove the voxel surface points from the pathfinding map
		CLDR->RemoveSurfaceVoxelPointsForChunk(chunkToDestroyPosition);
	} else {
		CLDR->RemoveSurfaceVoxelPointsForChunk(chunkToDestroyPosition);
	}
}

// Update the player's current position that will be used for pathfinding
void AChunkWorld::updatePlayerCurrentPosition(FVector& PlayerPosition) {
	if (updatePlayerCurrentPositionCounter >= updatePlayerCurrentPositionPerFrames) {
		WTSR->updateCurrentPlayerPosition(PlayerPosition);
		updatePlayerCurrentPositionCounter = 0;
	} else {
		updatePlayerCurrentPositionCounter++;
	}
}

// Called when the game starts or when spawned
void AChunkWorld::BeginPlay() {
	Super::BeginPlay();

	spawnInitialWorld();

	CacheVoxelBasicMaterial();

	generateTreeMeshVariations();
	generateGrassMeshVariations();
	generateFlowerMeshVariations();

	// Set player's initial position
	WTSR->UpdateInitialPlayerPosition(GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation());

	isInitialWorldGenerated = true;

	WTSR->printMapElements("Map after BeginPlay()");
}

void AChunkWorld::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	Super::EndPlay(EndPlayReason);

	if (PathfindingManager) {
		PathfindingManager->ShutDownThreadPool();
		PathfindingManager.Reset();
	}

	GenerationTaskManager.Shutdown();

	if (PerlinNoiseSettingsRef) {
		PerlinNoiseSettingsRef = nullptr;
	}

	if (WorldTerrainSettingsRef) {
		WorldTerrainSettingsRef = nullptr;
	}

	if (ChunkLocationDataRef) {
		ChunkLocationDataRef = nullptr;
	}

	if (AnimationSettingsRef) {
		AnimationSettingsRef = nullptr;
	}
}

FIntPoint AChunkWorld::GetChunkCoordinates(FVector Position) const {
	int32 ChunkX = FMath::FloorToInt(Position.X / (WTSR->chunkSize * WTSR->UnrealScale));
	int32 ChunkZ = FMath::FloorToInt(Position.Y / (WTSR->chunkSize * WTSR->UnrealScale));
	return FIntPoint(ChunkX, ChunkZ);
}

void AChunkWorld::LogChunkMeshComputeTime() {
	if (WTSR->chunksMeshCounter % 100 == 0 && WTSR->chunksMeshCounter != lastLoggedChunkCount) {
		float chunkSpawnTime = WTSR->chunkSpawnTime.count() / WTSR->chunksMeshCounter;
		int seconds = static_cast<int>(chunkSpawnTime) / 1000;
		int milliseconds = static_cast<int>(chunkSpawnTime) % 1000;

		UE_LOG(LogTemp, Warning, TEXT("Average mesh compute time for %d chunks: %d seconds, %d milliseconds."), WTSR->chunksMeshCounter, seconds, milliseconds);

		lastLoggedChunkCount = WTSR->chunksMeshCounter;
	}
}

bool AChunkWorld::CanRunWorldTick() const {
	if (WTSR == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("WTSR is nullptr!"));
		return false;
	}

	if (PNSR == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("PNSR is nullptr!"));
		return false;
	}

	if (CLDR == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("CLDR is nullptr!"));
		return false;
	}

	if (!isInitialWorldGenerated) {
		UE_LOG(LogTemp, Warning, TEXT("World not yet initialized. Tick() will exit now."));
		return false;
	}

	return true;
}

void AChunkWorld::HandlePerlinNoiseSettingsChanged() {
	if (PNSR->changedSettings) {
		isInitialWorldGenerated = false;

		destroyCurrentWorldChunks();

		// Rebuilding noise generators before queueing the initial world again
		SetPerlinNoiseSettings(PerlinNoiseSettingsRef);

		spawnInitialWorld();

		isInitialWorldGenerated = true;

		PNSR->changedSettings = false;
	}
}

void AChunkWorld::UpdatePlayerChunkStreaming(const FVector& PlayerPosition) {
	const FIntPoint PlayerChunkCoords = GetChunkCoordinates(PlayerPosition);
	const FIntPoint InitialChunkCoords = GetChunkCoordinates(WTSR->getInitialPlayerPosition());

	// Starting a location update only after the player crosses a chunk boundary
	const bool isPlayerMovingOnAxisX = PlayerChunkCoords.X != InitialChunkCoords.X;
	const bool isPlayerMovingOnAxisZ = PlayerChunkCoords.Y != InitialChunkCoords.Y;

	if (isPlayerMovingOnAxisX || isPlayerMovingOnAxisZ) {
		GenerationTaskManager.TryStartLocationTask(PlayerPosition, WTSR, CLDR, &GrassActorsToRemove, &FlowerActorsToRemove, &TreeActorsToRemove, &NpcActorsToRemove);
	}
}

void AChunkWorld::ProcessGenerationJobs() {
	// Completing finished async work on the game thread before using its results
	if (GenerationTaskManager.CompleteLocationTaskIfReady()) {
		onNewTerrainGenerated();
	}

	// Starting at most one mesh build task so generation remains controlled
	GenerationTaskManager.TryStartMeshTask(WTSR, CLDR, PNSR);

	if (GenerationTaskManager.CompleteMeshTaskIfReady()) {
		onNewTerrainGenerated();
	}
}

void AChunkWorld::ProcessChunkLifecycle(const FVector& PlayerPosition) {
	// Spawning and destroying one terrain chunk at a time to smooth streaming cost
	SpawnSingleChunk(PlayerPosition);
	UpdateChunksCollision();

	DestroySingleChunk();
}

void AChunkWorld::RefreshSpawnPointRanges() {
	if (FramesCounterCheckSpawnedPointsInRange > FramesToCheckForSpawnPointsInRange) {
		// Refreshing range caches periodically because these scans can touch many objects
		CLDR->CheckForSpawnPointsInRange();
		CLDR->CheckAndAddVegetationNotInRange(&GrassActorsToRemove, &FlowerActorsToRemove);
		CLDR->CheckAndAddTreesNotInRange(&TreeActorsToRemove);
		CLDR->CheckAndAddNpcsNotInRange(&NpcActorsToRemove);
		FramesCounterCheckSpawnedPointsInRange = 0;

		// Updating active spawn ranges used to validate cached spawn queues
		VegetationChunkSpawnPoints = CLDR->GetVegetationChunkSpawnPoints();
		TreeChunkSpawnPoints = CLDR->GetTreeChunkSpawnPoints();
		NpcChunkSpawnPoints = CLDR->GetNpcChunkSpawnPoints();
		TrimCachedSpawnPositions();
	}
	FramesCounterCheckSpawnedPointsInRange++;
}

void AChunkWorld::ProcessVegetationAndNpcSpawning(const FVector& PlayerPosition) {
	// Processing trees first because their collision can affect nearby gameplay sooner
	SpawnMultipleTreeObjects(PlayerPosition);
	DestroyTreeActors();

	UpdateTreesCollision();

	SpawnMultipleGrassObjects(PlayerPosition);
	SpawnMultipleFlowerObjects(PlayerPosition);
	SpawnMultipleNpcObjects(PlayerPosition);

	DestroyGrassActors();
	DestroyFlowerActors();
	DestroyNpcActors();
}

// Called every frame
void AChunkWorld::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);

	spawnedTreesThisFrame = false;
	spawnedChunksThisFrame = false;

	if (!CanRunWorldTick()) {
		return;
	}

	LogChunkMeshComputeTime();
	HandlePerlinNoiseSettingsChanged();

	FVector PlayerPosition = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	updatePlayerCurrentPosition(PlayerPosition);

	UpdatePlayerChunkStreaming(PlayerPosition);
	ProcessGenerationJobs();
	ProcessChunkLifecycle(PlayerPosition);

	// Giving terrain spawning the frame budget before processing lighter world objects
	if (spawnedChunksThisFrame) {
		return;
	}

	RefreshSpawnPointRanges();
	ProcessVegetationAndNpcSpawning(PlayerPosition);
}

void AChunkWorld::calculateAverageChunkSpawnTime(const Time& startTime, const Time& endTime) {
	std::chrono::duration<double, std::milli> duration = endTime - startTime;
	double chunkSpawnTime = duration.count();

	// Accumulate time and increment chunk count
	TotalTimeForChunks += chunkSpawnTime;
	ChunksSpawnedCount++;

	// Check if enough chunks spawned to calculate the average
	if (ChunksSpawnedCount >= ChunksToAverage) {
		double averageTime = TotalTimeForChunks / ChunksSpawnedCount;
		UE_LOG(LogTemp, Warning, TEXT("Average time to spawn a BinaryChunk: %f milliseconds; Calculated after spawning %d chunks."), averageTime, ChunksSpawnedCount);

		// Reset counters
		ChunksSpawnedCount = 0;
		TotalTimeForChunks = 0.0;
	}
}
