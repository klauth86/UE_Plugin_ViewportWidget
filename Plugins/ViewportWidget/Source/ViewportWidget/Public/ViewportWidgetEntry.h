// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "UObject/ObjectMacros.h"
#include "ViewportWidgetEntry.generated.h"

class AActor;

UENUM(BlueprintType)
enum class ECustomViewportType :uint8
{
	CVT_Perspective = 0		UMETA(DisplayName = "Perspective"),
	CVT_OrthoFreelook = 1	UMETA(DisplayName = "Ortho Free"),
	CVT_OrthoXY = 2			UMETA(DisplayName = "Ortho Top"),
	CVT_OrthoXZ = 3			UMETA(DisplayName = "Ortho Front"),
	CVT_OrthoYZ = 4			UMETA(DisplayName = "Ortho Left"),
	CVT_OrthoNegativeXY = 5	UMETA(DisplayName = "Ortho Bottom"),
	CVT_OrthoNegativeXZ = 6	UMETA(DisplayName = "Ortho Back"),
	CVT_OrthoNegativeYZ = 7	UMETA(DisplayName = "Ortho Right"),
};

//------------------------------------------------------
// FViewportWidgetEntry
//------------------------------------------------------

USTRUCT(BlueprintType)
struct VIEWPORTWIDGET_API FViewportWidgetEntry
{
	GENERATED_USTRUCT_BODY()

public:
	static const TArray<FViewportWidgetEntry>& GetEmptyCollection() { static TArray<FViewportWidgetEntry> emptyCollection; return emptyCollection; }

	FViewportWidgetEntry() :ActorClassPtr(nullptr), SpawnTransform(FTransform::Identity), ActorObjectPtr(nullptr) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftClassPtr<AActor> ActorClassPtr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform SpawnTransform;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AActor> ActorObjectPtr;
};