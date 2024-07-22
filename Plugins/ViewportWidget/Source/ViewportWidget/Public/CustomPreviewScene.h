// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "UObject/GCObject.h"

//------------------------------------------------------
// FCustomPreviewScene
//------------------------------------------------------

class VIEWPORTWIDGET_API FCustomPreviewScene : public FGCObject
{
public:
	struct ConstructionValues
	{
		ConstructionValues()
			: bDefaultLighting(true)
			, bAllowAudioPlayback(false)
			, bForceMipsResident(true)
			, bTransactional(true)
			, bForceUseMovementComponentInNonGameWorld(false)
		{}

		uint32 bDefaultLighting : 1;
		uint32 bAllowAudioPlayback : 1;
		uint32 bForceMipsResident : 1;
		uint32 bTransactional : 1;
		uint32 bForceUseMovementComponentInNonGameWorld : 1;

		TSubclassOf<class AGameModeBase> DefaultGameMode;
		class UGameInstance* OwningGameInstance = nullptr;

		ConstructionValues& SetCreateDefaultLighting(const bool bDefault) { bDefaultLighting = bDefault; return *this; }

		ConstructionValues& AllowAudioPlayback(const bool bAllow) { bAllowAudioPlayback = bAllow; return *this; }
		ConstructionValues& SetForceMipsResident(const bool bForce) { bForceMipsResident = bForce; return *this; }
		ConstructionValues& SetTransactional(const bool bInTransactional) { bTransactional = bInTransactional; return *this; }
		ConstructionValues& ForceUseMovementComponentInNonGameWorld(const bool bInForceUseMovementComponentInNonGameWorld) { bForceUseMovementComponentInNonGameWorld = bInForceUseMovementComponentInNonGameWorld; return *this; }

		ConstructionValues& SetDefaultGameMode(TSubclassOf<class AGameModeBase> GameMode) { DefaultGameMode = GameMode; return *this; }
		ConstructionValues& SetOwningGameInstance(class UGameInstance* InGameInstance) { OwningGameInstance = InGameInstance; return *this; }
	};

	// for physical correct light computations we multiply diffuse and specular lights by PI (see LABEL_RealEnergy)
	FCustomPreviewScene(ConstructionValues CVS = ConstructionValues());
	virtual ~FCustomPreviewScene();

	/**
	 * Adds a component to the preview scene.  This attaches the component to the scene, and takes ownership of it.
	 */
	virtual void AddComponent(class UActorComponent* Component, const FTransform& LocalToWorld, bool bAttachToRoot = false);

	/**
	 * Removes a component from the preview scene.  This detaches the component from the scene, and returns ownership of it.
	 */
	virtual void RemoveComponent(class UActorComponent* Component);

	// Serializer.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	// Accessors.
	UWorld* GetWorld() const { return PreviewWorld; }
	FSceneInterface* GetScene() const;

	/** Access to line drawing */
	class ULineBatchComponent* GetLineBatcher() const { return LineBatcher; }
	/** Clean out the line batcher each frame */
	void ClearLineBatcher();

	/** Update sky and reflection captures */
	void UpdateCaptureContents();

private:
	TArray<class UActorComponent*> Components;

protected:
	class UWorld* PreviewWorld = nullptr;
	class ULineBatchComponent* LineBatcher = nullptr;

	/** This controls whether or not all mip levels of textures used by UMeshComponents added to this preview window should be loaded and remain loaded. */
	bool bForceAllUsedMipsResident;
};