// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "ViewportWidgetModule.h"
#include "CustomViewportClient.h"
#include "CustomPreviewScene.h"
#include "Widgets/SViewportWidget.h"
#include "Components/ViewportWidget.h"

#include "UObject/UObjectGlobals.h"

#include "SceneView.h"
#include "SceneViewExtension.h"
#include "SceneManagement.h"

#include "BufferVisualizationData.h"
#include "NaniteVisualizationData.h"
#include "LumenVisualizationData.h"
#include "StrataVisualizationData.h"
#include "GroomVisualizationData.h"
#include "VirtualShadowMapVisualizationData.h"
#include "GPUSkinCacheVisualizationData.h"

#include "LegacyScreenPercentageDriver.h"
#include "Engine/World.h"
#include "CanvasTypes.h"
#include "UnrealEngine.h"
#include "EngineModule.h"
#include "Engine/RendererSettings.h"
#include "Components/LineBatchComponent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"

#include "AudioDeviceHandle.h"
#include "AudioDevice.h"
#include "Components/SkyLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "GameFramework/GameModeBase.h"

#define LOCTEXT_NAMESPACE "FInputSequenceToolsModule"

//------------------------------------------------------
// FCustomPreviewScene
//------------------------------------------------------

FCustomPreviewScene::FCustomPreviewScene(FCustomPreviewScene::ConstructionValues CVS)
	: PreviewWorld(nullptr)
	, bForceAllUsedMipsResident(CVS.bForceMipsResident)
{
	EObjectFlags NewObjectFlags = RF_NoFlags;
	if (CVS.bTransactional)
	{
		NewObjectFlags = RF_Transactional;
	}

	PreviewWorld = NewObject<UWorld>();
	PreviewWorld->SetFlags(NewObjectFlags);
	PreviewWorld->WorldType = EWorldType::Game;

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(PreviewWorld->WorldType);
	WorldContext.SetCurrentWorld(PreviewWorld);

	PreviewWorld->InitializeNewWorld(UWorld::InitializationValues()
		.AllowAudioPlayback(CVS.bAllowAudioPlayback)
		.CreatePhysicsScene(false)
		.RequiresHitProxies(false) // Only Need hit proxies in an editor scene
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false)
		.SetTransactional(CVS.bTransactional)
		.SetDefaultGameMode(CVS.DefaultGameMode)
		.ForceUseMovementComponentInNonGameWorld(CVS.bForceUseMovementComponentInNonGameWorld));

	FURL URL = FURL();
	//URL += TEXT("?SpectatorOnly=1");
	//URL = FURL(NULL, *EditorEngine->BuildPlayWorldURL(*PIEMapName, Params.bStartInSpectatorMode, ExtraURLOptions), TRAVEL_Absolute);

	PreviewWorld->InitializeActorsForPlay(URL);

	if (CVS.bDefaultLighting)
	{
		LineBatcher = NewObject<ULineBatchComponent>();
		LineBatcher->bCalculateAccurateBounds = false;
		AddComponent(LineBatcher, FTransform::Identity);
	}
}

FCustomPreviewScene::~FCustomPreviewScene()
{
	// Stop any audio components playing in this scene
	if (GEngine)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
			{
				AudioDevice->Flush(GetWorld(), false);
			}
		}
	}

	// Remove all the attached components
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UActorComponent* Component = Components[ComponentIndex];

		if (bForceAllUsedMipsResident)
		{
			// Remove the mip streaming override on the mesh to be removed
			UMeshComponent* pMesh = Cast<UMeshComponent>(Component);
			if (pMesh != NULL)
			{
				pMesh->SetTextureForceResidentFlag(false);
			}
		}

		Component->UnregisterComponent();
	}

	// The world may be released by now.
	if (PreviewWorld && GEngine)
	{
		PreviewWorld->CleanupWorld();
		GEngine->DestroyWorldContext(GetWorld());
	}
}

void FCustomPreviewScene::AddComponent(UActorComponent* Component, const FTransform& LocalToWorld, bool bAttachToRoot /*= false*/)
{
	Components.AddUnique(Component);

	USceneComponent* SceneComp = Cast<USceneComponent>(Component);
	if (SceneComp && SceneComp->GetAttachParent() == NULL)
	{
		SceneComp->SetRelativeTransform(LocalToWorld);
	}

	Component->RegisterComponentWithWorld(GetWorld());

	if (bForceAllUsedMipsResident)
	{
		// Add a mip streaming override to the new mesh
		UMeshComponent* pMesh = Cast<UMeshComponent>(Component);
		if (pMesh != NULL)
		{
			pMesh->SetTextureForceResidentFlag(true);
		}
	}

	{
		UStaticMeshComponent* pStaticMesh = Cast<UStaticMeshComponent>(Component);
		if (pStaticMesh != nullptr)
		{
			pStaticMesh->bEvaluateWorldPositionOffset = true;
			pStaticMesh->bEvaluateWorldPositionOffsetInRayTracing = true;
		}
	}

	GetScene()->UpdateSpeedTreeWind(0.0);
}

void FCustomPreviewScene::RemoveComponent(UActorComponent* Component)
{
	Component->UnregisterComponent();
	Components.Remove(Component);

	if (bForceAllUsedMipsResident)
	{
		// Remove the mip streaming override on the old mesh
		UMeshComponent* pMesh = Cast<UMeshComponent>(Component);
		if (pMesh != NULL)
		{
			pMesh->SetTextureForceResidentFlag(false);
		}
	}
}

void FCustomPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Components);
	Collector.AddReferencedObject(PreviewWorld);
}

FString FCustomPreviewScene::GetReferencerName() const
{
	return TEXT("FCustomPreviewScene");
}

FSceneInterface* FCustomPreviewScene::GetScene() const { return PreviewWorld->Scene; }

void FCustomPreviewScene::UpdateCaptureContents()
{
	// This function is called from FAdvancedPreviewScene::Tick, FBlueprintEditor::Tick, and FThumbnailPreviewScene::Tick,
	// so assume we are inside a Tick function.
	const bool bInsideTick = true;

	USkyLightComponent::UpdateSkyCaptureContents(PreviewWorld);
	UReflectionCaptureComponent::UpdateReflectionCaptureContents(PreviewWorld, nullptr, false, false, bInsideTick);
}

void FCustomPreviewScene::ClearLineBatcher()
{
	if (LineBatcher != NULL)
	{
		LineBatcher->Flush();
	}
}

//------------------------------------------------------
// SViewportWidget
//------------------------------------------------------

bool IsNotEqual(const TArray<FViewportWidgetEntry>& A, const TArray<FViewportWidgetEntry>& B)
{
	if (A.Num() != B.Num())
	{
		return true;
	}

	for (size_t i = 0; i < A.Num(); i++)
	{
		if ((A[i].ActorClassPtr != B[i].ActorClassPtr) || (GetTypeHash(A[i].SpawnTransform) != GetTypeHash(B[i].SpawnTransform)))
		{
			return true;
		}
	}

	return false;
}

SViewportWidget::SViewportWidget() :LastTickTime(0), PreviewScene(MakeShareable(new FCustomPreviewScene(FCustomPreviewScene::ConstructionValues().SetForceMipsResident(false)))) {}

SViewportWidget::~SViewportWidget()
{
	// Close viewport
	if (Client.IsValid())
	{
		Client->Viewport = NULL;
	}

	// Release our reference to the viewport client
	Client.Reset();

	check(SceneViewport.IsUnique());
}

void SViewportWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SAssignNew(ViewportWidget, SViewport)
				.EnableGammaCorrection(false) // Scene rendering handles this
				.ViewportSize(InArgs._ViewportSize)
		];

	Client = MakeViewportClient();

	if (!Client->VisibilityDelegate.IsBound())
	{
		Client->VisibilityDelegate.BindSP(this, &SViewportWidget::IsVisible);
	}

	SceneViewport = MakeShareable(new FSceneViewport(Client.Get(), ViewportWidget));
	Client->Viewport = SceneViewport.Get();
	ViewportWidget->SetViewportInterface(SceneViewport.ToSharedRef());

	SetViewTransform(InArgs._ViewTransform.Get(FTransform::Identity));

	SetEntries(const_cast<TArray<FViewportWidgetEntry>&>(InArgs._Entries.Get()));
}

void SViewportWidget::SetViewTransform(const FTransform& viewTransform)
{
	if (!ViewTransform.IsSet() || GetTypeHash(ViewTransform.Get()) != GetTypeHash(viewTransform))
	{
		ViewTransform = viewTransform;

		Client->SetViewLocation(viewTransform.GetLocation());
		Client->SetViewRotation(viewTransform.Rotator());
	}
}

void SViewportWidget::SetEntries(TArray<FViewportWidgetEntry>& entries)
{
	if (!Entries.IsSet() || IsNotEqual(Entries.Get(), entries))
	{
		CleanEntries();
		Entries = entries;
		AddEntries();
	}
}

void SViewportWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	LastTickTime = FPlatformTime::Seconds();

	if (PreviewScene.IsValid())
	{
		PreviewScene->UpdateCaptureContents();
		PreviewScene->ClearLineBatcher();
	}

	if (Client.IsValid())
	{
		Client->GetWorld()->Tick(ELevelTick::LEVELTICK_All, InDeltaTime);

		{
			FScopedConditionalWorldSwitcher WorldSwitcher(Client->GetWorld());

			Client->Tick(InDeltaTime);
		}

		Client->Viewport->Draw();
	}
}

bool SViewportWidget::IsVisible() const
{
#if WITH_DUMPGPU
	extern ENGINE_API uint32 GDumpGPU_FrameNumber;
#endif

	const float VisibilityTimeThreshold = .25f;
	// The viewport is visible if we don't have a parent layout (likely a floating window) or this viewport is visible in the parent layout.
	// Also, always render the viewport if DumpGPU is active, regardless of tick time threshold -- otherwise these don't show up due to lag
	// caused by the GPU dump being triggered.
	return
		ViewportWidget.IsValid() &&
		LastTickTime == 0.0 ||	// Never been ticked
		FPlatformTime::Seconds() - LastTickTime <= VisibilityTimeThreshold	// Ticked recently
#if WITH_DUMPGPU
		|| GDumpGPU_FrameNumber == GFrameNumber	// GPU dump in progress
#endif		
		;
}

TWeakObjectPtr<AActor> SViewportWidget::GetSpawnedActor(const int32 entryIndex) const
{
	if (Entries.IsSet())
	{
		if (Entries.Get().IsValidIndex(entryIndex))
		{
			return Entries.Get()[entryIndex].ActorObjectPtr;
		}
	}

	return TWeakObjectPtr<AActor>();
}

TSharedRef<FCustomViewportClient> SViewportWidget::MakeViewportClient()
{
	TSharedPtr<FCustomViewportClient> client = MakeShareable(new FCustomViewportClient(PreviewScene.Get(), SharedThis(this)));

	client->SetRealtime(true);
	client->VisibilityDelegate.BindSP(this, &SViewportWidget::IsVisible);

	return client.ToSharedRef();
}

void SViewportWidget::CleanEntries()
{
	if (UWorld* world = PreviewScene ? PreviewScene->GetWorld() : nullptr)
	{
		if (Entries.IsSet())
		{
			for (FViewportWidgetEntry& ViewportWidgetEntry : const_cast<TArray<FViewportWidgetEntry>&>(Entries.Get()))
			{
				if (AActor* actor = ViewportWidgetEntry.ActorObjectPtr.Get())
				{
					world->DestroyActor(actor);
				}

				ViewportWidgetEntry.ActorObjectPtr.Reset();
			}
		}
	}
}

void SViewportWidget::AddEntries()
{
	if (UWorld* world = PreviewScene ? PreviewScene->GetWorld() : nullptr)
	{
		if (Entries.IsSet())
		{
			for (FViewportWidgetEntry& ViewportWidgetEntry : const_cast<TArray<FViewportWidgetEntry>&>(Entries.Get()))
			{
				if (TSubclassOf<AActor> actorClass = ViewportWidgetEntry.ActorClassPtr.LoadSynchronous())
				{
					FActorSpawnParameters SpawnInfo;
					SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					SpawnInfo.bNoFail = true;
					SpawnInfo.ObjectFlags = RF_Transient | RF_Transactional;

					AActor* actor = world->SpawnActor(actorClass, &ViewportWidgetEntry.SpawnTransform, SpawnInfo);

					ViewportWidgetEntry.ActorObjectPtr = actor;

					SetupSpawnedActor(actor, world);
				}
			}
		}
	}
}

//------------------------------------------------------
// UViewportWidget
//------------------------------------------------------

void UViewportWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MyViewportWidget.IsValid())
	{
		MyViewportWidget->SetViewTransform(ViewTransform);
		MyViewportWidget->SetEntries(Entries);
	}
}

void UViewportWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	MyViewportWidget.Reset();

	Super::ReleaseSlateResources(bReleaseChildren);
}

#if WITH_EDITOR
const FText UViewportWidget::GetPaletteCategory()
{
	return LOCTEXT("Advanced", "Advanced");
}
#endif

void UViewportWidget::SetViewTransform(FTransform viewTransform)
{
	ViewTransform = viewTransform;

	if (MyViewportWidget.IsValid())
	{
		MyViewportWidget->SetViewTransform(ViewTransform);
	}
}

void UViewportWidget::SetEntries(const TArray<FViewportWidgetEntry>& entries)
{
	Entries = entries;

	if (MyViewportWidget.IsValid())
	{
		MyViewportWidget->SetEntries(Entries);
	}
}

AActor* UViewportWidget::GetSpawnedActor(const int32 entryIndex) const
{
	if (MyViewportWidget.IsValid())
	{
		return MyViewportWidget->GetSpawnedActor(entryIndex).IsValid() ? MyViewportWidget->GetSpawnedActor(entryIndex).Get() : nullptr;
	}

	return nullptr;
}

TSharedRef<SWidget> UViewportWidget::RebuildWidget()
{
	MyViewportWidget = SNew(SViewportWidget)
		.ViewTransform(ViewTransform)
		.Entries(Entries);
	return MyViewportWidget.ToSharedRef();
}

//------------------------------------------------------
// FCustomViewportClient
//------------------------------------------------------

/** Parameter struct for editor viewport view modifiers */
struct FCustomViewportViewModifierParams
{
	FMinimalViewInfo ViewInfo;

	void AddPostProcessBlend(const FPostProcessSettings& Settings, float Weight)
	{
		check(PostProcessSettings.Num() == PostProcessBlendWeights.Num());
		PostProcessSettings.Add(Settings);
		PostProcessBlendWeights.Add(Weight);
	}

private:
	TArray<FPostProcessSettings> PostProcessSettings;
	TArray<float> PostProcessBlendWeights;

	friend class FCustomViewportClient;
};

static int32 ViewOptionIndex = 0;
static TArray<ECustomViewportType> ViewOptions;

void InitViewOptionsArray()
{
	ViewOptions.Empty();

	ECustomViewportType Front = ECustomViewportType::CVT_OrthoXZ;
	ECustomViewportType Back = ECustomViewportType::CVT_OrthoNegativeXZ;
	ECustomViewportType Top = ECustomViewportType::CVT_OrthoXY;
	ECustomViewportType Bottom = ECustomViewportType::CVT_OrthoNegativeXY;
	ECustomViewportType Left = ECustomViewportType::CVT_OrthoYZ;
	ECustomViewportType Right = ECustomViewportType::CVT_OrthoNegativeYZ;

	ViewOptions.Add(Front);
	ViewOptions.Add(Back);
	ViewOptions.Add(Top);
	ViewOptions.Add(Bottom);
	ViewOptions.Add(Left);
	ViewOptions.Add(Right);
}

const EViewModeIndex FCustomViewportClient::DefaultPerspectiveViewMode = VMI_Lit;
const EViewModeIndex FCustomViewportClient::DefaultOrthoViewMode = VMI_BrushWireframe;

float ComputeOrthoZoomFactor(const float ViewportWidth)
{
	float Ret = 1.0f;

	// We want to have all ortho view ports scale the same way to have the axis aligned with each other.
	// So we take out the usual scaling of a view based on it's width.
	// That means when a view port is resized in x or y it shows more content, not the same content larger (for x) or has no effect (for y).
	// 500 is to get good results with existing view port settings.
	Ret = ViewportWidth / 500.0f;

	return Ret;
}

namespace OrbitConstants_NM
{
	const float OrbitPanSpeed = 1.0f;
	const float IntialLookAtDistance = 1024.f;
}

namespace CustomViewportDefs_NM
{
	/** Default camera field of view angle for level editor perspective viewports */
	inline const float DefaultPerspectiveFOVAngle(90.0f);
}

FCustomViewportClient::FCustomViewportClient(FCustomPreviewScene* InPreviewScene, const TWeakPtr<SViewportWidget>& InViewportWidget)
	: ImmersiveDelegate()
	, VisibilityDelegate()
	, Viewport(NULL)
	, ViewportType(ECustomViewportType::CVT_Perspective)
	, ViewState()
	, StereoViewStates()
	, EngineShowFlags(ESFIM_Editor)
	, LastEngineShowFlags(ESFIM_Game)
	, ExposureSettings()
	, CurrentBufferVisualizationMode(NAME_None)
	, CurrentNaniteVisualizationMode(NAME_None)
	, CurrentLumenVisualizationMode(NAME_None)
	, CurrentStrataVisualizationMode(NAME_None)
	, CurrentGroomVisualizationMode(NAME_None)
	, CurrentVirtualShadowMapVisualizationMode(NAME_None)
	, CurrentRayTracingDebugVisualizationMode(NAME_None)
	, CurrentGPUSkinCacheVisualizationMode(NAME_None)
	, ViewFOV(CustomViewportDefs_NM::DefaultPerspectiveFOVAngle)
	, FOVAngle(CustomViewportDefs_NM::DefaultPerspectiveFOVAngle)
	, bForcingUnlitForNewMap(false)
	, bNeedsRedraw(true)
	, LandscapeLODOverride(-1)
	, TimeForForceRedraw(0.0)
	, CurrentMousePos(-1, -1)
	, bIsRealtime(false)
	, ViewportWidget(InViewportWidget)
	, PreviewScene(InPreviewScene)
	, PerspViewModeIndex(DefaultPerspectiveViewMode)
	, OrthoViewModeIndex(DefaultOrthoViewMode)
	, ViewModeParam(-1)
	, NearPlane(-1.0f)
	, FarPlane(0.0f)
	, bInGameViewMode(false)
{
	InitViewOptionsArray();

	FSceneInterface* Scene = GetScene();
	ViewState.Allocate(Scene ? Scene->GetFeatureLevel() : GMaxRHIFeatureLevel);

	// NOTE: StereoViewState will be allocated on demand, for viewports than end up drawing in stereo

	// Most editor viewports do not want motion blur.
	EngineShowFlags.MotionBlur = 0;

	EngineShowFlags.SetSnap(1);

	SetViewMode(IsPerspective() ? PerspViewModeIndex : OrthoViewModeIndex);

	RequestUpdateDPIScale();

#if WITH_EDITOR
	FSlateApplication::Get().OnWindowDPIScaleChanged().AddRaw(this, &FCustomViewportClient::HandleWindowDPIScaleChanged);
#endif
}

FCustomViewportClient::~FCustomViewportClient()
{
	if (Viewport)
	{
		UE_LOG(LogTemp, Fatal, TEXT("Viewport != NULL in FCustomViewportClient destructor."));
	}

	if (FSlateApplication::IsInitialized())
	{
#if WITH_EDITOR
		FSlateApplication::Get().OnWindowDPIScaleChanged().RemoveAll(this);
#endif
	}
}

bool FCustomViewportClient::ToggleRealtime()
{
	SetRealtime(!bIsRealtime);
	return bIsRealtime;
}

float FCustomViewportClient::GetOrthoUnitsPerPixel(const FViewport* InViewport) const
{
	const float SizeX = static_cast<float>(InViewport->GetSizeXY().X);

	// 15.0f was coming from the CAMERA_ZOOM_DIV marco, seems it was chosen arbitrarily
	return (1 / (SizeX * 15.f)) * ComputeOrthoZoomFactor(SizeX);
}

void FCustomViewportClient::SetInitialViewTransform(ECustomViewportType InViewportType, const FVector& ViewLocation, const FRotator& ViewRotation)
{
	FCustomViewportCameraTransform& ViewTransform = (InViewportType == ECustomViewportType::CVT_Perspective) ? ViewTransformPerspective : ViewTransformOrthographic;

	ViewTransform.SetLocation(ViewLocation);
	ViewTransform.SetRotation(ViewRotation);

	// Make a look at location in front of the camera
	const FQuat CameraOrientation = FQuat::MakeFromEuler(ViewRotation.Euler());
	FVector Direction = CameraOrientation.RotateVector(FVector(1, 0, 0));

	ViewTransform.SetLookAt(ViewLocation + Direction * OrbitConstants_NM::IntialLookAtDistance);
}

//////////////////////////////////////////////////////////////////////////
//
// Configures the specified FSceneView object with the view and projection matrices for this viewport.

FSceneView* FCustomViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex)
{
	const bool bStereoRendering = StereoViewIndex != INDEX_NONE;

	FSceneViewInitOptions ViewInitOptions;

	FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
	const ECustomViewportType EffectiveViewportType = GetViewportType();

	// Apply view modifiers.
	FCustomViewportViewModifierParams ViewModifierParams;
	{
		ViewModifierParams.ViewInfo.Location = ViewTransform.GetLocation();
		ViewModifierParams.ViewInfo.Rotation = ViewTransform.GetRotation();

		ViewModifierParams.ViewInfo.FOV = ViewFOV;
	}
	const FVector ModifiedViewLocation = ViewModifierParams.ViewInfo.Location;
	FRotator ModifiedViewRotation = ViewModifierParams.ViewInfo.Rotation;
	const float ModifiedViewFOV = ViewModifierParams.ViewInfo.FOV;

	ViewInitOptions.ViewOrigin = ModifiedViewLocation;

	FIntPoint ViewportSize = Viewport->GetSizeXY();
	ViewportSize.X = FMath::Max(ViewportSize.X, 1);
	ViewportSize.Y = FMath::Max(ViewportSize.Y, 1);
	FIntPoint ViewportOffset(0, 0);

	ViewInitOptions.SetViewRectangle(FIntRect(ViewportOffset, ViewportOffset + ViewportSize));

	EAspectRatioAxisConstraint AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MajorAxisFOV;

	AWorldSettings* WorldSettings = nullptr;
	if (GetScene() != nullptr && GetScene()->GetWorld() != nullptr)
	{
		WorldSettings = GetScene()->GetWorld()->GetWorldSettings();
	}
	if (WorldSettings != nullptr)
	{
		ViewInitOptions.WorldToMetersScale = WorldSettings->WorldToMeters;
	}

	{
		//
		if (EffectiveViewportType == ECustomViewportType::CVT_Perspective)
		{
			// If stereo rendering is enabled, update the size and offset appropriately for this pass
			// @todo vreditor: Also need to update certain other use cases of ViewFOV like culling, streaming, etc.  (needs accessor)
			if (bStereoRendering)
			{
				int32 X = 0;
				int32 Y = 0;
				uint32 SizeX = ViewportSize.X;
				uint32 SizeY = ViewportSize.Y;
				GEngine->StereoRenderingDevice->AdjustViewRect(StereoViewIndex, X, Y, SizeX, SizeY);
				const FIntRect StereoViewRect = FIntRect(X, Y, X + SizeX, Y + SizeY);
				ViewInitOptions.SetViewRectangle(StereoViewRect);

				GEngine->StereoRenderingDevice->CalculateStereoViewOffset(StereoViewIndex, ModifiedViewRotation, ViewInitOptions.WorldToMetersScale, ViewInitOptions.ViewOrigin);
			}

			// Calc view rotation matrix
			ViewInitOptions.ViewRotationMatrix = CalcViewRotationMatrix(ModifiedViewRotation);

			// Rotate view 90 degrees
			ViewInitOptions.ViewRotationMatrix = ViewInitOptions.ViewRotationMatrix * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));

			if (bStereoRendering)
			{
				// @todo vreditor: bConstrainAspectRatio is ignored in this path, as it is in the game client as well currently
				// Let the stereoscopic rendering device handle creating its own projection matrix, as needed
				ViewInitOptions.ProjectionMatrix = GEngine->StereoRenderingDevice->GetStereoProjectionMatrix(StereoViewIndex);
				ViewInitOptions.StereoPass = GEngine->StereoRenderingDevice->GetViewPassForIndex(bStereoRendering, StereoViewIndex);
			}
			else
			{
				const float MinZ = GetNearClipPlane();
				const float MaxZ = MinZ;
				// Avoid zero ViewFOV's which cause divide by zero's in projection matrix
				const float MatrixFOV = FMath::Max(0.001f, ModifiedViewFOV) * (float)PI / 360.0f;

				{
					float XAxisMultiplier;
					float YAxisMultiplier;

					if (((ViewportSize.X > ViewportSize.Y) && (AspectRatioAxisConstraint == AspectRatio_MajorAxisFOV)) || (AspectRatioAxisConstraint == AspectRatio_MaintainXFOV))
					{
						//if the viewport is wider than it is tall
						XAxisMultiplier = 1.0f;
						YAxisMultiplier = ViewportSize.X / (float)ViewportSize.Y;
					}
					else
					{
						//if the viewport is taller than it is wide
						XAxisMultiplier = ViewportSize.Y / (float)ViewportSize.X;
						YAxisMultiplier = 1.0f;
					}

					if ((bool)ERHIZBuffer::IsInverted)
					{
						ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
							MatrixFOV,
							MatrixFOV,
							XAxisMultiplier,
							YAxisMultiplier,
							MinZ,
							MaxZ
						);
					}
					else
					{
						ViewInitOptions.ProjectionMatrix = FPerspectiveMatrix(
							MatrixFOV,
							MatrixFOV,
							XAxisMultiplier,
							YAxisMultiplier,
							MinZ,
							MaxZ
						);
					}
				}
			}
		}
		else
		{
			static_assert((bool)ERHIZBuffer::IsInverted, "Check all the Rotation Matrix transformations!");
			FMatrix::FReal ZScale = 0.5f / UE_OLD_HALF_WORLD_MAX;	// LWC_TODO: WORLD_MAX misuse?
			FMatrix::FReal ZOffset = UE_OLD_HALF_WORLD_MAX;

			//The divisor for the matrix needs to match the translation code.
			const float Zoom = GetOrthoUnitsPerPixel(Viewport);

			float OrthoWidth = Zoom * ViewportSize.X / 2.0f;
			float OrthoHeight = Zoom * ViewportSize.Y / 2.0f;

			if (EffectiveViewportType == ECustomViewportType::CVT_OrthoXY)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(1, 0, 0, 0),
					FPlane(0, -1, 0, 0),
					FPlane(0, 0, -1, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoXZ)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(1, 0, 0, 0),
					FPlane(0, 0, -1, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoYZ)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(0, 0, 1, 0),
					FPlane(1, 0, 0, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoNegativeXY)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(-1, 0, 0, 0),
					FPlane(0, -1, 0, 0),
					FPlane(0, 0, 1, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoNegativeXZ)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(-1, 0, 0, 0),
					FPlane(0, 0, 1, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoNegativeYZ)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(0, 0, -1, 0),
					FPlane(-1, 0, 0, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, 0, 1));
			}
			else if (EffectiveViewportType == ECustomViewportType::CVT_OrthoFreelook)
			{
				ViewInitOptions.ViewRotationMatrix = FMatrix(
					FPlane(0, 0, 1, 0),
					FPlane(1, 0, 0, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, 0, 1));
			}
			else
			{
				// Unknown viewport type
				check(false);
			}

			ViewInitOptions.ProjectionMatrix = FReversedZOrthoMatrix(
				OrthoWidth,
				OrthoHeight,
				ZScale,
				ZOffset
			);
		}
	}

	if (!ViewInitOptions.IsValidViewRectangle())
	{
		// Zero sized rects are invalid, so fake to 1x1 to avoid asserts later on
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, 1, 1));
	}

	// Allocate our stereo view state on demand, so that only viewports that actually use stereo features have one
	const int32 ViewStateIndex = (StereoViewIndex != INDEX_NONE) ? StereoViewIndex : 0;
	if (bStereoRendering)
	{
		if (StereoViewStates.Num() <= ViewStateIndex)
		{
			StereoViewStates.SetNum(ViewStateIndex + 1);
		}

		if (StereoViewStates[ViewStateIndex].GetReference() == nullptr)
		{
			FSceneInterface* Scene = GetScene();
			StereoViewStates[ViewStateIndex].Allocate(Scene ? Scene->GetFeatureLevel() : GMaxRHIFeatureLevel);
		}
	}

	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.SceneViewStateInterface = ((ViewStateIndex == 0) ? ViewState.GetReference() : StereoViewStates[ViewStateIndex].GetReference());
	ViewInitOptions.StereoViewIndex = StereoViewIndex;

	ViewInitOptions.BackgroundColor = GetBackgroundColor();

	// for ortho views to steal perspective view origin
	ViewInitOptions.bUseFauxOrthoViewPos = true;

	ViewInitOptions.FOV = ModifiedViewFOV;

	ViewInitOptions.OverrideFarClippingPlaneDistance = FarPlane;
	ViewInitOptions.CursorPos = CurrentMousePos;

	FSceneView* View = new FSceneView(ViewInitOptions);

	View->ViewLocation = ModifiedViewLocation;
	View->ViewRotation = ModifiedViewRotation;

	int32 FamilyIndex = ViewFamily->Views.Add(View);
	check(FamilyIndex == View->StereoViewIndex || View->StereoViewIndex == INDEX_NONE);

	View->StartFinalPostprocessSettings(View->ViewLocation);

	OverridePostProcessSettings(*View);

	if (ViewModifierParams.ViewInfo.PostProcessBlendWeight > 0.f)
	{
		View->OverridePostProcessSettings(ViewModifierParams.ViewInfo.PostProcessSettings, ViewModifierParams.ViewInfo.PostProcessBlendWeight);
	}
	const int32 PPNum = FMath::Min(ViewModifierParams.PostProcessSettings.Num(), ViewModifierParams.PostProcessBlendWeights.Num());
	for (int32 PPIndex = 0; PPIndex < PPNum; ++PPIndex)
	{
		const FPostProcessSettings& PPSettings = ViewModifierParams.PostProcessSettings[PPIndex];
		const float PPWeight = ViewModifierParams.PostProcessBlendWeights[PPIndex];
		View->OverridePostProcessSettings(PPSettings, PPWeight);
	}

	View->EndFinalPostprocessSettings(ViewInitOptions);

	for (int ViewExt = 0; ViewExt < ViewFamily->ViewExtensions.Num(); ViewExt++)
	{
		ViewFamily->ViewExtensions[ViewExt]->SetupView(*ViewFamily, *View);
	}

	return View;
}

void FCustomViewportClient::Tick(float DeltaTime)
{
	// refresh ourselves if animating or told to from another view
	if (TimeForForceRedraw > 0)
	{
		TimeForForceRedraw -= DeltaTime;

		if (TimeForForceRedraw < 0)
		{
			Invalidate();
		}
	}
}

void FCustomViewportClient::SetViewportType(ECustomViewportType InViewportType)
{
	ViewportType = InViewportType;

	// Changing the type may also change the active view mode; re-apply that now
	ApplyViewMode(GetViewMode(), IsPerspective(), EngineShowFlags);

	// We might have changed to an orthographic viewport; if so, update any viewport links
	UpdateLinkedOrthoViewports(true);

	Invalidate();
}

void FCustomViewportClient::RotateViewportType()
{
	ViewportType = ViewOptions[ViewOptionIndex];

	// Changing the type may also change the active view mode; re-apply that now
	ApplyViewMode(GetViewMode(), IsPerspective(), EngineShowFlags);

	// We might have changed to an orthographic viewport; if so, update any viewport links
	UpdateLinkedOrthoViewports(true);

	Invalidate();

	if (ViewOptionIndex == 5)
	{
		ViewOptionIndex = 0;
	}
	else
	{
		ViewOptionIndex++;
	}
}

bool FCustomViewportClient::IsActiveViewportTypeInRotation() const { return GetViewportType() == ViewOptions[ViewOptionIndex]; }

bool FCustomViewportClient::IsPerspective() const { return (GetViewportType() == ECustomViewportType::CVT_Perspective); }
/**
 * Forcibly disables lighting show flags if there are no lights in the scene, or restores lighting show
 * flags if lights are added to the scene.
 */
void FCustomViewportClient::UpdateLightingShowFlags(FEngineShowFlags& InOutShowFlags)
{
	bool bViewportNeedsRefresh = false;

	if (bForcingUnlitForNewMap && !bInGameViewMode && IsPerspective())
	{
		// We'll only use default lighting for viewports that are viewing the main world
		if (GWorld != NULL && GetScene() != NULL && GetScene()->GetWorld() != NULL && GetScene()->GetWorld() == GWorld)
		{
			// Check to see if there are any lights in the scene
			bool bAnyLights = GetScene()->HasAnyLights();
			if (bAnyLights)
			{
				// Is unlit mode currently enabled?  We'll make sure that all of the regular unlit view
				// mode show flags are set (not just EngineShowFlags.Lighting), so we don't disrupt other view modes
				if (!InOutShowFlags.Lighting)
				{
					// We have lights in the scene now so go ahead and turn lighting back on
					// designer can see what they're interacting with!
					InOutShowFlags.SetLighting(true);
				}

				// No longer forcing lighting to be off
				bForcingUnlitForNewMap = false;
			}
			else
			{
				// Is lighting currently enabled?
				if (InOutShowFlags.Lighting)
				{
					// No lights in the scene, so make sure that lighting is turned off so the level
					// designer can see what they're interacting with!
					InOutShowFlags.SetLighting(false);
				}
			}
		}
	}
}

void FCustomViewportClient::HandleWindowDPIScaleChanged(TSharedRef<SWindow> InWindow)
{
	RequestUpdateDPIScale();
	Invalidate();
}

void FCustomViewportClient::HandleToggleShowFlag(FEngineShowFlags::EShowFlag EngineShowFlagIndex)
{
	const bool bOldState = EngineShowFlags.GetSingleFlag(EngineShowFlagIndex);
	EngineShowFlags.SetSingleFlag(EngineShowFlagIndex, !bOldState);

	// Invalidate clients which aren't real-time so we see the changes.
	Invalidate();
}

void FCustomViewportClient::ChangeBufferVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeBuffer);
	CurrentBufferVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentBufferVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeBuffer), TEXT("In order to call GetCurrentBufferVisualizationMode(), first you must set ViewMode to VMI_VisualizeBuffer."));
	return (CurrentBufferVisualizationMode.IsNone()
		? FBufferVisualizationData::GetMaterialDefaultDisplayName() : GetBufferVisualizationData().GetMaterialDisplayName(CurrentBufferVisualizationMode));
}

void FCustomViewportClient::ChangeNaniteVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeNanite);
	CurrentNaniteVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentNaniteVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeNanite), TEXT("In order to call GetCurrentNaniteVisualizationMode(), first you must set ViewMode to VMI_VisualizeNanite."));
	return GetNaniteVisualizationData().GetModeDisplayName(CurrentNaniteVisualizationMode);
}

void FCustomViewportClient::ChangeLumenVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeLumen);
	CurrentLumenVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentLumenVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeLumen), TEXT("In order to call GetCurrentLumenVisualizationMode(), first you must set ViewMode to VMI_VisualizeLumen."));
	return GetLumenVisualizationData().GetModeDisplayName(CurrentLumenVisualizationMode);
}

void FCustomViewportClient::ChangeVirtualShadowMapVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeVirtualShadowMap);
	CurrentVirtualShadowMapVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentVirtualShadowMapVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeVirtualShadowMap), TEXT("In order to call GetCurrentVirtualShadowMapVisualizationMode(), first you must set ViewMode to VMI_VisualizeVirtualShadowMap."));
	return GetVirtualShadowMapVisualizationData().GetModeDisplayName(CurrentVirtualShadowMapVisualizationMode);
}

void FCustomViewportClient::ChangeStrataVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeSubstrate);
	CurrentStrataVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentStrataVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeSubstrate), TEXT("In order to call GetCurrentSubstrateVisualizationMode(), first you must set ViewMode to VMI_VisualizeSubstrate."));
	return GetStrataVisualizationData().GetModeDisplayName(CurrentStrataVisualizationMode);
}

void FCustomViewportClient::ChangeGroomVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeGroom);
	CurrentGroomVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentGroomVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeGroom), TEXT("In order to call GetCurrentGroomVisualizationMode(), first you must set ViewMode to VMI_VisualizeGroom."));
	return GetGroomVisualizationData().GetModeDisplayName(CurrentGroomVisualizationMode);
}
bool FCustomViewportClient::IsVisualizeCalibrationMaterialEnabled() const
{
	// Get the list of requested buffers from the console
	const URendererSettings* Settings = GetDefault<URendererSettings>();
	check(Settings);

	return ((EngineShowFlags.VisualizeCalibrationCustom && Settings->VisualizeCalibrationCustomMaterialPath.IsValid()) ||
		(EngineShowFlags.VisualizeCalibrationColor && Settings->VisualizeCalibrationColorMaterialPath.IsValid()) ||
		(EngineShowFlags.VisualizeCalibrationGrayscale && Settings->VisualizeCalibrationGrayscaleMaterialPath.IsValid()));
}

void FCustomViewportClient::ChangeRayTracingDebugVisualizationMode(FName InName)
{
	SetViewMode(VMI_RayTracingDebug);
	CurrentRayTracingDebugVisualizationMode = InName;
}

void FCustomViewportClient::ChangeGPUSkinCacheVisualizationMode(FName InName)
{
	SetViewMode(VMI_VisualizeGPUSkinCache);
	CurrentGPUSkinCacheVisualizationMode = InName;
}

FText FCustomViewportClient::GetCurrentGPUSkinCacheVisualizationModeDisplayName() const
{
	checkf(IsViewModeEnabled(VMI_VisualizeGPUSkinCache), TEXT("In order to call GetCurrentGPUSkinCacheVisualizationMode(), first you must set ViewMode to VMI_VisualizeGPUSkinCache."));
	return GetGPUSkinCacheVisualizationData().GetModeDisplayName(CurrentGPUSkinCacheVisualizationMode);
}

bool FCustomViewportClient::SupportsPreviewResolutionFraction() const
{
	// Don't do preview screen percentage for some view mode.
	switch (GetViewMode())
	{
	case VMI_BrushWireframe:
	case VMI_Wireframe:
	case VMI_LightComplexity:
	case VMI_LightmapDensity:
	case VMI_LitLightmapDensity:
	case VMI_ReflectionOverride:
	case VMI_StationaryLightOverlap:
	case VMI_CollisionPawn:
	case VMI_CollisionVisibility:
	case VMI_LODColoration:
	case VMI_PrimitiveDistanceAccuracy:
	case VMI_MeshUVDensityAccuracy:
	case VMI_HLODColoration:
	case VMI_GroupLODColoration:
	case VMI_VisualizeGPUSkinCache:
		return false;
	}

	// Don't do preview screen percentage in certain cases.
	if (EngineShowFlags.VisualizeBuffer || EngineShowFlags.VisualizeNanite || EngineShowFlags.VisualizeVirtualShadowMap || IsVisualizeCalibrationMaterialEnabled())
	{
		return false;
	}

	return true;
}


float FCustomViewportClient::GetDefaultPrimaryResolutionFractionTarget() const
{
	FStaticResolutionFractionHeuristic StaticHeuristic(EngineShowFlags);

#if WITH_EDITOR
	if (FStaticResolutionFractionHeuristic::FUserSettings::EditorOverridePIESettings())
	{
		StaticHeuristic.Settings.PullEditorRenderingSettings(bIsRealtime, /* bIsPathTraced = */ EngineShowFlags.PathTracing);
	}
	else
#endif
	{
		StaticHeuristic.Settings.PullRunTimeRenderingSettings();
	}

	StaticHeuristic.TotalDisplayedPixelCount = FMath::Max(Viewport->GetSizeXY().X * Viewport->GetSizeXY().Y, 1);
	StaticHeuristic.DPIScale = GetDPIScale();
	return StaticHeuristic.ResolveResolutionFraction();
}

int32 FCustomViewportClient::GetPreviewScreenPercentage() const
{
	float ResolutionFraction = 1.0f;
	if (PreviewResolutionFraction.IsSet())
	{
		ResolutionFraction = PreviewResolutionFraction.GetValue();
	}
	else
	{
		ResolutionFraction = GetDefaultPrimaryResolutionFractionTarget();
	}

	// We expose the resolution fraction derived from DPI, to not lie to the artist when screen percentage = 100%.
	return FMath::RoundToInt(FMath::Clamp(
		ResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction) * 100.0f);
}

void FCustomViewportClient::SetPreviewScreenPercentage(int32 PreviewScreenPercentage)
{
	float AutoResolutionFraction = GetDefaultPrimaryResolutionFractionTarget();
	int32 AutoScreenPercentage = FMath::RoundToInt(FMath::Clamp(
		AutoResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction) * 100.0f);

	float NewResolutionFraction = PreviewScreenPercentage / 100.0f;
	if (NewResolutionFraction >= ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction &&
		NewResolutionFraction <= ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction &&
		PreviewScreenPercentage != AutoScreenPercentage)
	{
		PreviewResolutionFraction = NewResolutionFraction;
	}
	else
	{
		PreviewResolutionFraction.Reset();
	}
}

/** Convert the specified number (in cm or unreal units) into a readable string with relevant si units */
FString FCustomViewportClient::UnrealUnitsToSiUnits(float UnrealUnits)
{
	// Put it in mm to start off with
	UnrealUnits *= 10.f;

	const int32 OrderOfMagnitude = UnrealUnits > 0 ? FMath::TruncToInt(FMath::LogX(10.0f, UnrealUnits)) : 0;

	// Get an exponent applied to anything >= 1,000,000,000mm (1000km)
	const int32 Exponent = (OrderOfMagnitude - 6) / 3;
	const FString ExponentString = Exponent > 0 ? FString::Printf(TEXT("e+%d"), Exponent * 3) : TEXT("");

	float ScaledNumber = UnrealUnits;

	// Factor the order of magnitude into thousands and clamp it to km
	const int32 OrderOfThousands = OrderOfMagnitude / 3;
	if (OrderOfThousands != 0)
	{
		// Scale units to m or km (with the order of magnitude in 1000s)
		ScaledNumber /= FMath::Pow(1000.f, OrderOfThousands);
	}

	// Round to 2 S.F.
	const TCHAR* Approximation = TEXT("");
	{
		const int32 ScaledOrder = OrderOfMagnitude % (FMath::Max(OrderOfThousands, 1) * 3);
		const float RoundingDivisor = FMath::Pow(10.f, ScaledOrder) / 10.f;
		const int32 Rounded = FMath::TruncToInt(ScaledNumber / RoundingDivisor) * RoundingDivisor;
		if (ScaledNumber - Rounded > KINDA_SMALL_NUMBER)
		{
			ScaledNumber = Rounded;
			Approximation = TEXT("~");
		}
	}

	if (OrderOfMagnitude <= 2)
	{
		// Always show cm not mm
		ScaledNumber /= 10;
	}

	static const TCHAR* UnitText[] = { TEXT("cm"), TEXT("m"), TEXT("km") };
	if (FMath::Fmod(ScaledNumber, 1.f) > KINDA_SMALL_NUMBER)
	{
		return FString::Printf(TEXT("%s%.1f%s%s"), Approximation, ScaledNumber, *ExponentString, UnitText[FMath::Min(OrderOfThousands, 2)]);
	}
	else
	{
		return FString::Printf(TEXT("%s%d%s%s"), Approximation, FMath::TruncToInt(ScaledNumber), *ExponentString, UnitText[FMath::Min(OrderOfThousands, 2)]);
	}
}

void FCustomViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (PreviewScene)
	{
		PreviewScene->AddReferencedObjects(Collector);
	}

	if (ViewState.GetReference())
	{
		ViewState.GetReference()->AddReferencedObjects(Collector);
	}

	for (FSceneViewStateReference& StereoViewState : StereoViewStates)
	{
		if (StereoViewState.GetReference())
		{
			StereoViewState.GetReference()->AddReferencedObjects(Collector);
		}
	}
}

FSceneInterface* FCustomViewportClient::GetScene() const
{
	UWorld* World = GetWorld();
	if (World)
	{
		return World->Scene;
	}

	return NULL;
}

FLinearColor FCustomViewportClient::GetBackgroundColor() const { return FColor(55, 55, 55); }

UWorld* FCustomViewportClient::GetWorld() const
{
	UWorld* OutWorldPtr = NULL;
	// If we have a valid scene get its world
	if (PreviewScene)
	{
		OutWorldPtr = PreviewScene->GetWorld();
	}
	if (OutWorldPtr == NULL)
	{
		OutWorldPtr = GWorld;
	}
	return OutWorldPtr;
}

void FCustomViewportClient::SetupViewForRendering(FSceneViewFamily& ViewFamily, FSceneView& View)
{
	if (ViewFamily.EngineShowFlags.Wireframe)
	{
		// Wireframe color is emissive-only, and mesh-modifying materials do not use material substitution, hence...
		View.DiffuseOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
		View.SpecularOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
	}
	else if (ViewFamily.EngineShowFlags.OverrideDiffuseAndSpecular)
	{
		View.DiffuseOverrideParameter = FVector4f(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View.SpecularOverrideParameter = FVector4f(.1f, .1f, .1f, 0.0f);
	}
	else if (ViewFamily.EngineShowFlags.LightingOnlyOverride)
	{
		View.DiffuseOverrideParameter = FVector4f(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View.SpecularOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
	}
	else if (ViewFamily.EngineShowFlags.ReflectionOverride)
	{
		View.DiffuseOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
		View.SpecularOverrideParameter = FVector4f(1, 1, 1, 0.0f);
		View.NormalOverrideParameter = FVector4f(0, 0, 1, 0.0f);
		View.RoughnessOverrideParameter = FVector2D(0.0f, 0.0f);
	}

	if (!ViewFamily.EngineShowFlags.Diffuse)
	{
		View.DiffuseOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
	}

	if (!ViewFamily.EngineShowFlags.Specular)
	{
		View.SpecularOverrideParameter = FVector4f(0.f, 0.f, 0.f, 0.f);
	}

	View.CurrentBufferVisualizationMode = CurrentBufferVisualizationMode;
	View.CurrentNaniteVisualizationMode = CurrentNaniteVisualizationMode;
	View.CurrentLumenVisualizationMode = CurrentLumenVisualizationMode;
	View.CurrentStrataVisualizationMode = CurrentStrataVisualizationMode;
	View.CurrentGroomVisualizationMode = CurrentGroomVisualizationMode;
	View.CurrentVirtualShadowMapVisualizationMode = CurrentVirtualShadowMapVisualizationMode;
	View.CurrentGPUSkinCacheVisualizationMode = CurrentGPUSkinCacheVisualizationMode;
#if RHI_RAYTRACING
	View.CurrentRayTracingDebugVisualizationMode = CurrentRayTracingDebugVisualizationMode;
#endif
}

void FCustomViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FViewport* ViewportBackup = Viewport;
	Viewport = InViewport ? InViewport : Viewport;

	UWorld* World = GetWorld();
	FGameTime Time;
	if (!World || (GetScene() != World->Scene) || UseAppTime())
	{
		Time = FGameTime::GetTimeSinceAppStart();
	}
	else
	{
		Time = World->GetTime();
	}

	// Early out if we are changing maps in editor as there is no reason to render the scene and it may not even be valid (For unsaved maps)
	if (World && World->IsPreparingMapChange())
	{
		return;
	}

	// Allow HMD to modify the view later, just before rendering
	const bool bStereoRendering = GEngine->IsStereoscopic3D(InViewport);
	Canvas->SetScaledToRenderTarget(bStereoRendering);
	Canvas->SetStereoRendering(bStereoRendering);

	FEngineShowFlags UseEngineShowFlags = EngineShowFlags;
	if (OverrideShowFlagsFunc)
	{
		OverrideShowFlagsFunc(UseEngineShowFlags);
	}

	// Setup a FSceneViewFamily/FSceneView for the viewport.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Canvas->GetRenderTarget(),
		GetScene(),
		UseEngineShowFlags)
		.SetTime(Time)
		.SetRealtimeUpdate(IsRealtime() && FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
		.SetViewModeParam(ViewModeParam, ViewModeParamName));

	ViewFamily.DebugDPIScale = GetDPIScale();

	ViewFamily.EngineShowFlags = UseEngineShowFlags;

	ViewFamily.bIsHDR = Viewport->IsHDRViewport();

	// The view is in focus if it is currently in editing
	ViewFamily.SetIsInFocus(false);

	if (!UseEngineShowFlags.Game)
	{
		// in the editor, disable camera motion blur and other rendering features that rely on the former frame
		// unless the view port is cinematic controlled
		ViewFamily.EngineShowFlags.CameraInterpolation = 0;
	}

	if (!bStereoRendering)
	{
		// stereo is enabled, as many HMDs require this for proper visuals
		ViewFamily.EngineShowFlags.SetScreenPercentage(false);
	}

	FSceneViewExtensionContext ViewExtensionContext(InViewport);
	ViewExtensionContext.bStereoEnabled = true;
	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);

	for (auto ViewExt : ViewFamily.ViewExtensions)
	{
		ViewExt->SetupViewFamily(ViewFamily);
	}

	EViewModeIndex CurrentViewMode = GetViewMode();
	ViewFamily.ViewMode = CurrentViewMode;

	const bool bVisualizeBufferEnabled = CurrentViewMode == VMI_VisualizeBuffer && CurrentBufferVisualizationMode != NAME_None;
	const bool bRayTracingDebugEnabled = CurrentViewMode == VMI_RayTracingDebug && CurrentRayTracingDebugVisualizationMode != NAME_None;
	const bool bVisualizeGPUSkinCache = CurrentViewMode == VMI_VisualizeGPUSkinCache && CurrentGPUSkinCacheVisualizationMode != NAME_None;
	const bool bCanDisableTonemapper = bVisualizeBufferEnabled || bVisualizeGPUSkinCache;

	EngineShowFlagOverride(ESFIM_Editor, ViewFamily.ViewMode, ViewFamily.EngineShowFlags, bCanDisableTonemapper);
	EngineShowFlagOrthographicOverride(IsPerspective(), ViewFamily.EngineShowFlags);

	UpdateLightingShowFlags(ViewFamily.EngineShowFlags);

	ViewFamily.ExposureSettings = ExposureSettings;

	ViewFamily.LandscapeLODOverride = LandscapeLODOverride;

	FSceneView* View = nullptr;

	// Stereo rendering
	const bool bStereoDeviceActive = bStereoRendering && GEngine->StereoRenderingDevice.IsValid();
	int32 NumViews = bStereoRendering ? GEngine->StereoRenderingDevice->GetDesiredNumberOfViews(bStereoRendering) : 1;
	for (int StereoViewIndex = 0; StereoViewIndex < NumViews; ++StereoViewIndex)
	{
		View = CalcSceneView(&ViewFamily, bStereoRendering ? StereoViewIndex : INDEX_NONE);

		SetupViewForRendering(ViewFamily, *View);

		FSlateRect SafeFrame;
		View->CameraConstrainedViewRect = View->UnscaledViewRect;

		const float SizeX = InViewport->GetSizeXY().X / GetDPIScale();
		const float SizeY = InViewport->GetSizeXY().Y / GetDPIScale();

		SafeFrame = FSlateRect(0, 0, SizeX, SizeY);

		if (World)
		{
			FWorldCachedViewInfo& WorldViewInfo = World->CachedViewInfoRenderedLastFrame.AddDefaulted_GetRef();
			WorldViewInfo.ViewMatrix = View->ViewMatrices.GetViewMatrix();
			WorldViewInfo.ProjectionMatrix = View->ViewMatrices.GetProjectionMatrix();
			WorldViewInfo.ViewProjectionMatrix = View->ViewMatrices.GetViewProjectionMatrix();
			WorldViewInfo.ViewToWorld = View->ViewMatrices.GetInvViewMatrix();
			World->LastRenderTime = World->GetTimeSeconds();
		}
	}

	{
		// If a screen percentage interface was not set by one of the view extension, then set the legacy one.
		if (ViewFamily.GetScreenPercentageInterface() == nullptr)
		{
			float GlobalResolutionFraction = 1.0f;

			// Apply preview resolution fraction. Supported in stereo for VR Editor Mode only
			if ((!bStereoRendering) &&
				SupportsPreviewResolutionFraction() && ViewFamily.SupportsScreenPercentage())
			{
				if (PreviewResolutionFraction.IsSet())
				{
					GlobalResolutionFraction = PreviewResolutionFraction.GetValue();
				}
				else
				{
					GlobalResolutionFraction = GetDefaultPrimaryResolutionFractionTarget();
				}

				// Force screen percentage's engine show flag to be turned on for preview screen percentage.
				ViewFamily.EngineShowFlags.ScreenPercentage = (GlobalResolutionFraction != 1.0);
			}

			// In editor viewport, we ignore r.ScreenPercentage and FPostProcessSettings::ScreenPercentage by design.
			ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
				ViewFamily, GlobalResolutionFraction));
		}

		check(ViewFamily.GetScreenPercentageInterface() != nullptr);
	}

	// Draw the 3D scene
	GetRendererModule().BeginRenderingViewFamily(Canvas, &ViewFamily);

	// Remove temporary debug lines.
	// Possibly a hack. Lines may get added without the scene being rendered etc.
	if (World && World->LineBatcher != NULL && (World->LineBatcher->BatchedLines.Num() || World->LineBatcher->BatchedPoints.Num() || World->LineBatcher->BatchedMeshes.Num()))
	{
		World->LineBatcher->Flush();
	}

	if (World && World->ForegroundLineBatcher != NULL && (World->ForegroundLineBatcher->BatchedLines.Num() || World->ForegroundLineBatcher->BatchedPoints.Num() || World->ForegroundLineBatcher->BatchedMeshes.Num()))
	{
		World->ForegroundLineBatcher->Flush();
	}

	if (!IsRealtime())
	{
		// Wait for the rendering thread to finish drawing the view before returning.
		// This reduces the apparent latency of dragging the viewport around.
		FlushRenderingCommands();
	}

	Viewport = ViewportBackup;
}

/** True if the window is maximized or floating */
bool FCustomViewportClient::IsVisible() const
{
	bool bIsVisible = false;

	if (VisibilityDelegate.IsBound())
	{
		// Call the visibility delegate to see if our parent viewport and layout configuration says we arevisible
		bIsVisible = VisibilityDelegate.Execute();
	}

	return bIsVisible;
}

void FCustomViewportClient::GetViewportDimensions(FIntPoint& OutOrigin, FIntPoint& Outize)
{
	OutOrigin = FIntPoint(0, 0);
	if (Viewport != NULL)
	{
		Outize.X = Viewport->GetSizeXY().X;
		Outize.Y = Viewport->GetSizeXY().Y;
	}
	else
	{
		Outize = FIntPoint(0, 0);
	}
}

void FCustomViewportClient::SetViewMode(EViewModeIndex InViewModeIndex)
{
	ViewModeParam = -1; // Reset value when the viewmode changes
	ViewModeParamName = NAME_None;
	ViewModeParamNameMap.Empty();

	if (IsPerspective())
	{
		PerspViewModeIndex = InViewModeIndex;
		ApplyViewMode(PerspViewModeIndex, true, EngineShowFlags);
		bForcingUnlitForNewMap = false;
	}
	else
	{
		OrthoViewModeIndex = InViewModeIndex;
		ApplyViewMode(OrthoViewModeIndex, false, EngineShowFlags);
	}

	Invalidate();
}

void FCustomViewportClient::SetViewModes(const EViewModeIndex InPerspViewModeIndex, const EViewModeIndex InOrthoViewModeIndex)
{
	PerspViewModeIndex = InPerspViewModeIndex;
	OrthoViewModeIndex = InOrthoViewModeIndex;

	if (IsPerspective())
	{
		ApplyViewMode(PerspViewModeIndex, true, EngineShowFlags);
	}
	else
	{
		ApplyViewMode(OrthoViewModeIndex, false, EngineShowFlags);
	}

	Invalidate();
}

void FCustomViewportClient::SetViewModeParam(int32 InViewModeParam)
{
	ViewModeParam = InViewModeParam;
	FName* BoundName = ViewModeParamNameMap.Find(ViewModeParam);
	ViewModeParamName = BoundName ? *BoundName : FName();

	Invalidate();
}

bool FCustomViewportClient::IsViewModeParam(int32 InViewModeParam) const
{
	const FName* MappedName = ViewModeParamNameMap.Find(ViewModeParam);
	// Check if the param and names match. The param name only gets updated on click, while the map is built at menu creation.
	if (MappedName)
	{
		return ViewModeParam == InViewModeParam && ViewModeParamName == *MappedName;
	}
	else
	{
		return ViewModeParam == InViewModeParam && ViewModeParamName == NAME_None;
	}
}

void FCustomViewportClient::Invalidate(bool bInvalidateChildViews, bool bInvalidateHitProxies)
{
	if (Viewport)
	{
		if (bInvalidateHitProxies)
		{
			// Invalidate hit proxies and display pixels.
			Viewport->Invalidate();
		}
		else
		{
			// Invalidate only display pixels.
			Viewport->InvalidateDisplay();
		}
	}
}

void FCustomViewportClient::SetGameView(bool bGameViewEnable)
{
	// backup this state as we want to preserve it
	bool bCompositeEditorPrimitives = EngineShowFlags.CompositeEditorPrimitives;

	// defaults
	FEngineShowFlags GameFlags(ESFIM_Game);
	FEngineShowFlags EditorFlags(ESFIM_Editor);
	{
		// likely we can take the existing state
		if (EngineShowFlags.Game)
		{
			GameFlags = EngineShowFlags;
			EditorFlags = LastEngineShowFlags;
		}
		else if (LastEngineShowFlags.Game)
		{
			GameFlags = LastEngineShowFlags;
			EditorFlags = EngineShowFlags;
		}
	}

	// toggle between the game and engine flags
	if (bGameViewEnable)
	{
		EngineShowFlags = GameFlags;
		LastEngineShowFlags = EditorFlags;
	}
	else
	{
		EngineShowFlags = EditorFlags;
		LastEngineShowFlags = GameFlags;
	}

	// maintain this state
	EngineShowFlags.SetCompositeEditorPrimitives(bCompositeEditorPrimitives);
	LastEngineShowFlags.SetCompositeEditorPrimitives(bCompositeEditorPrimitives);

	//reset game engine show flags that may have been turned on by making a selection in game view
	if (bGameViewEnable)
	{
		EngineShowFlags.SetModeWidgets(false);
		EngineShowFlags.SetSelection(false);
	}

	EngineShowFlags.SetSelectionOutline(false);

	ApplyViewMode(GetViewMode(), IsPerspective(), EngineShowFlags);

	bInGameViewMode = bGameViewEnable;

	Invalidate();
}

float FCustomViewportClient::UpdateViewportClientWindowDPIScale() const
{
	float DPIScale = 1.f;
	if (ViewportWidget.IsValid())
	{
		TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.Pin().ToSharedRef());
		if (WidgetWindow.IsValid())
		{
			DPIScale = WidgetWindow->GetNativeWindow()->GetDPIScaleFactor();
		}
	}

	return DPIScale;
}

//------------------------------------------------------
// FViewportWidgetModule
//------------------------------------------------------

void FViewportWidgetModule::StartupModule()
{
}

void FViewportWidgetModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FViewportWidgetModule, ViewportWidget)