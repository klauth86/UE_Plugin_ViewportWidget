// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "ViewportClient.h"
#include "ShowFlags.h"

class FSceneViewFamily;
class FCustomPreviewScene;
class SViewportWidget;

enum class ECustomViewportType :uint8;

//------------------------------------------------------
// FCustomViewportCameraTransform
//------------------------------------------------------

struct VIEWPORTWIDGET_API FCustomViewportCameraTransform
{
public:
	FCustomViewportCameraTransform()
		: ViewLocation(FVector::ZeroVector)
		, ViewRotation(FRotator::ZeroRotator)
		, LookAt(FVector::ZeroVector)
	{}

	/** Sets the transform's location */
	void SetLocation(const FVector& Position) { ViewLocation = Position; }

	/** Sets the transform's rotation */
	void SetRotation(const FRotator& Rotation) { ViewRotation = Rotation; }

	/** Sets the location to look at during orbit */
	void SetLookAt(const FVector& InLookAt) { LookAt = InLookAt; }

	/** @return The transform's location */
	FORCEINLINE const FVector& GetLocation() const { return ViewLocation; }

	/** @return The transform's rotation */
	FORCEINLINE const FRotator& GetRotation() const { return ViewRotation; }

	/** @return The look at point for orbiting */
	FORCEINLINE const FVector& GetLookAt() const { return LookAt; }

	FMatrix ComputeOrbitMatrix() const
	{
		FTransform Transform =
			FTransform(-LookAt) *
			FTransform(FRotator(0, ViewRotation.Yaw, 0)) *
			FTransform(FRotator(0, 0, ViewRotation.Pitch)) *
			FTransform(FVector(0, (ViewLocation - LookAt).Size(), 0));

		return Transform.ToMatrixNoScale() * FInverseRotationMatrix(FRotator(0, 90.f, 0));
	}

private:
	/** Current viewport Position. */
	FVector	ViewLocation;
	/** Current Viewport orientation; valid only for perspective projections. */
	FRotator ViewRotation;
	/** When orbiting, the point we are looking at */
	FVector LookAt;
};

//------------------------------------------------------
// FCustomViewportClient
//------------------------------------------------------

DECLARE_DELEGATE_RetVal(bool, FCustomViewportStateGetter);

class VIEWPORTWIDGET_API FCustomViewportClient : public FCommonViewportClient, public FGCObject
{
public:
	FCustomViewportClient(FCustomPreviewScene* InPreviewScene = nullptr, const TWeakPtr<SViewportWidget>& InCustomViewportWidget = nullptr);
	virtual ~FCustomViewportClient();

	/** Non-copyable */
	FCustomViewportClient(const FCustomViewportClient&) = delete;
	FCustomViewportClient& operator=(const FCustomViewportClient&) = delete;

	/**
	 * Retrieves the FPreviewScene used by this instance of FCustomViewportClient.
	 *
	 * @return		The internal FPreviewScene pointer.
	 */
	FCustomPreviewScene* GetPreviewScene() { return PreviewScene; }

	/**
	 * Toggles whether or not the viewport updates in realtime and returns the updated state.
	 * Note: This value is saved between editor sessions so it should not be used for temporary states.  For that see SetRealtimeOverride
	 *
	 * @return		The current state of the realtime flag.
	 */
	bool ToggleRealtime();

	/**
	 * Sets whether or not the viewport updates in realtime.
	 * Note: This value is saved between editor sessions so it should not be used for temporary states.  For that see SetRealtimeOverride
	 */
	void SetRealtime(bool bInRealtime) { bIsRealtime = bInRealtime; }

	/** @return		True if viewport is in realtime mode, false otherwise. */
	bool IsRealtime() const { return bIsRealtime; }

	/** Gets ViewportCameraTransform object for the current viewport type */
	FCustomViewportCameraTransform& GetViewTransform() { return IsPerspective() ? ViewTransformPerspective : ViewTransformOrthographic; }

	const FCustomViewportCameraTransform& GetViewTransform() const { return IsPerspective() ? ViewTransformPerspective : ViewTransformOrthographic; }

	/** Sets the location of the viewport's camera */
	void SetViewLocation(const FVector& NewLocation)
	{
		FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
		ViewTransform.SetLocation(NewLocation);
	}

	/** Sets the location of the viewport's camera */
	void SetViewRotation(const FRotator& NewRotation)
	{
		FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
		ViewTransform.SetRotation(NewRotation);
	}

	/**
	 * Sets the look at location of the viewports camera for orbit *
	 *
	 * @param LookAt The new look at location
	 * @param bRecalulateView	If true, will recalculate view location and rotation to look at the new point immediatley
	 */
	void SetLookAtLocation(const FVector& LookAt, bool bRecalculateView = false)
	{
		FCustomViewportCameraTransform& ViewTransform = GetViewTransform();

		ViewTransform.SetLookAt(LookAt);

		if (bRecalculateView)
		{
			FMatrix OrbitMatrix = ViewTransform.ComputeOrbitMatrix();
			OrbitMatrix = OrbitMatrix.InverseFast();

			ViewTransform.SetRotation(OrbitMatrix.Rotator());
			ViewTransform.SetLocation(OrbitMatrix.GetOrigin());
		}
	}

	/** @return the current viewport camera location */
	const FVector& GetViewLocation() const
	{
		const FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
		return ViewTransform.GetLocation();
	}

	/** @return the current viewport camera rotation */
	const FRotator& GetViewRotation() const
	{
		const FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
		return ViewTransform.GetRotation();
	}

	/** @return the current look at location */
	const FVector& GetLookAtLocation() const
	{
		const FCustomViewportCameraTransform& ViewTransform = GetViewTransform();
		return ViewTransform.GetLookAt();
	}

	/** @return The number of units per pixel displayed in this viewport */
	float GetOrthoUnitsPerPixel(const FViewport* Viewport) const;

	/** Get a prettified string representation of the specified unreal units */
	static FString UnrealUnitsToSiUnits(float UnrealUnits);

	void SetInitialViewTransform(ECustomViewportType ViewportType, const FVector& ViewLocation, const FRotator& ViewRotation);

	/** FViewElementDrawer interface */
	virtual void Draw(FViewport* Viewport, FCanvas* Canvas) override;

	/** FViewportClient interface */
	virtual void RedrawRequested(FViewport* InViewport) override { bNeedsRedraw = true; }
	virtual void RequestInvalidateHitProxy(FViewport* InViewport) override {}
	virtual bool IsOrtho() const override { return !IsPerspective(); }

public:

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCustomViewportClient"); }

	/**
	 * Called to do any additional set up of the view for rendering
	 *
	 * @param ViewFamily	The view family being rendered
	 * @param View			The view being rendered
	 */
	virtual void SetupViewForRendering(FSceneViewFamily& ViewFamily, FSceneView& View);

	/**
	 * Configures the specified FSceneView object with the view and projection matrices for this viewport.
	 * @param	View		The view to be configured.  Must be valid.
	 * @param	StereoPass	Which eye we're drawing this view for when in stereo mode
	 * @return	A pointer to the view within the view family which represents the viewport's primary view.
	 */
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex = INDEX_NONE);

	/**
	 * @return The scene being rendered in this viewport
	 */
	virtual FSceneInterface* GetScene() const;

	/**
	 * @return The background color of the viewport
	 */
	virtual FLinearColor GetBackgroundColor() const;

	/**
	 * Called to override any post process settings for the view
	 *
	 * @param View	The view to override post process settings on
	 */
	virtual void OverridePostProcessSettings(FSceneView& View) {};

	/**
	 * Ticks this viewport client
	 */
	virtual void Tick(float DeltaSeconds);

	/**
	* Use the viewports Scene to get a world.
	* Will use Global world instance of the scene or its world is invalid.
	*
	* @return 		A valid pointer to the viewports world scene.
	*/
	virtual UWorld* GetWorld() const override;

	/** If true, this is a level editor viewport */
	virtual bool IsLevelEditorClient() const { return false; }

	virtual void UpdateLinkedOrthoViewports(bool bInvalidate = false) {}

	/**
	 * Normally we disable all viewport rendering when the editor is minimized, but the
	 * render commands may need to be processed regardless (like if we were outputting to a monitor via capture card).
	 * This provides the viewport a way to keep rendering, regardless of the editor's window status.
	 */
	virtual bool WantsDrawWhenAppIsHidden() const { return false; }

	/** Should this viewport use app time instead of world time. */
	virtual bool UseAppTime() const { return IsRealtime(); }

public:
	/** True if the window is maximized or floating */
	bool IsVisible() const;

	/**
	 * Sets how the viewport is displayed (lit, wireframe, etc) for the current viewport type
	 *
	 * @param	InViewModeIndex				View mode to set for the current viewport type
	 */
	virtual void SetViewMode(EViewModeIndex InViewModeIndex);

	/**
	 * Sets how the viewport is displayed (lit, wireframe, etc)
	 *
	 * @param	InPerspViewModeIndex		View mode to set when this viewport is of type CVT_Perspective
	 * @param	InOrthoViewModeIndex		View mode to set when this viewport is not of type CVT_Perspective
	 */
	void SetViewModes(const EViewModeIndex InPerspViewModeIndex, const EViewModeIndex InOrthoViewModeIndex);

	/** Set the viewmode param. */
	void SetViewModeParam(int32 InViewModeParam);

	/**
	 * @return The current view mode in this viewport, for the current viewport type
	 */
	EViewModeIndex GetViewMode() const { return (IsPerspective()) ? PerspViewModeIndex : OrthoViewModeIndex; }

	/**
	 * @return The view mode to use when this viewport is of type CVT_Perspective
	 */
	EViewModeIndex GetPerspViewMode() const { return PerspViewModeIndex; }

	/**
	 * @return The view mode to use when this viewport is not of type CVT_Perspective
	 */
	EViewModeIndex GetOrthoViewMode() const { return OrthoViewModeIndex; }

	/** @return True if InViewModeIndex is the current view mode index */
	bool IsViewModeEnabled(EViewModeIndex InViewModeIndex) const { return GetViewMode() == InViewModeIndex; }

	/** @return True if InViewModeIndex is the current view mode param */
	bool IsViewModeParam(int32 InViewModeParam) const;

	/**
	 * Invalidates this viewport and optionally child views.
	 *
	 * @param	bInvalidateChildViews		[opt] If true (the default), invalidate views that see this viewport as their parent.
	 * @param	bInvalidateHitProxies		[opt] If true (the default), invalidates cached hit proxies too.
	 */
	void Invalidate(bool bInvalidateChildViews = true, bool bInvalidateHitProxies = true);

	/**
	 * Gets the dimensions of the viewport
	 */
	void GetViewportDimensions(FIntPoint& OutOrigin, FIntPoint& OutSize);

	/**
	 * Returns the effective viewport type (taking into account any actor locking or camera possession)
	 */
	virtual ECustomViewportType GetViewportType() const { return ViewportType; }

	/**
	 * Set the viewport type of the client
	 *
	 * @param InViewportType	The viewport type to set the client to
	 */
	virtual void SetViewportType(ECustomViewportType InViewportType);

	/**
	 * Rotate through viewport view options
	 */
	virtual void RotateViewportType();

	/**
	* @return If the viewport option in the array is the active viewport type
	*/
	bool IsActiveViewportTypeInRotation() const;

	/**
	 * @return If InViewportType is the active viewport type
	 */
	bool IsActiveViewportType(ECustomViewportType InViewportType) const { return GetViewportType() == InViewportType; }

	/** Returns true if this viewport is perspective. */
	bool IsPerspective() const;

	/** @return True if the window is in an immersive viewport */
	bool IsInImmersiveViewport() const { return ImmersiveDelegate.IsBound() ? ImmersiveDelegate.Execute() : false; }

	/** Get the editor viewport widget */
	TSharedPtr<SViewportWidget> GetViewportWidget() const { return ViewportWidget.Pin(); }

	/**
	 * Computes a matrix to use for viewport location and rotation
	 */
	virtual FMatrix CalcViewRotationMatrix(const FRotator& InViewRotation) const { return FInverseRotationMatrix(InViewRotation); }

public:

	void SetGameView(bool bGameViewEnable);

	/**
	 * Returns true if this viewport is excluding non-game elements from its display
	 */
	virtual bool IsInGameView() const override { return bInGameViewMode; }

	/**
	 * Aspect ratio bar display settings
	 */
	void SetShowAspectRatioBarDisplay(bool bEnable)
	{
		EngineShowFlags.SetCameraAspectRatioBars(bEnable);
		Invalidate(false, false);
	}

	void SetShowSafeFrameBoxDisplay(bool bEnable)
	{
		EngineShowFlags.SetCameraSafeFrames(bEnable);
		Invalidate(false, false);
	}

	bool IsShowingAspectRatioBarDisplay() const { return EngineShowFlags.CameraAspectRatioBars == 1; }

	bool IsShowingSafeFrameBoxDisplay() const { return EngineShowFlags.CameraSafeFrames == 1; }

	/** Get the near clipping plane for this viewport. */
	float GetNearClipPlane() const { return (NearPlane < 0.0f) ? GNearClippingPlane : NearPlane; }

	/** Override the near clipping plane. Set to a negative value to disable the override. */
	void OverrideNearClipPlane(float InNearPlane) { NearPlane = InNearPlane; }

	/** Get the far clipping plane override for this viewport. */
	float GetFarClipPlaneOverride() const { return FarPlane; }

	/** Override the far clipping plane. Set to a negative value to disable the override. */
	void OverrideFarClipPlane(const float InFarPlane) { FarPlane = InFarPlane; }

	/** Returns the map allowing to convert from the viewmode param to a name. */
	TMap<int32, FName>& GetViewModeParamNameMap() { return ViewModeParamNameMap; }

	/** Delegate handler fired when a show flag is toggled */
	virtual void HandleToggleShowFlag(FEngineShowFlags::EShowFlag EngineShowFlagIndex);

	/** Delegate handler fired to determine the state of a show flag */
	virtual bool HandleIsShowFlagEnabled(FEngineShowFlags::EShowFlag EngineShowFlagIndex) const { return EngineShowFlags.GetSingleFlag(EngineShowFlagIndex); }

	/**
	 * Changes the buffer visualization mode for this viewport.
	 *
	 * @param InName	The ID of the required visualization mode
	 */
	void ChangeBufferVisualizationMode(FName InName);

	/**
	 * Checks if a buffer visualization mode is selected.
	 *
	 * @param InName	The ID of the required visualization mode
	 * @return	true if the supplied buffer visualization mode is checked
	 */
	bool IsBufferVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeBuffer) && CurrentBufferVisualizationMode == InName; }

	/**
	 * Returns the FText display name associated with CurrentBufferVisualizationMode.
	 */
	FText GetCurrentBufferVisualizationModeDisplayName() const;

	/**
	 * Changes the Nanite visualization mode for this viewport.
	 *
	 * @param InName	The ID of the required visualization mode
	 */
	void ChangeNaniteVisualizationMode(FName InName);

	/**
	 * Checks if a Nanite visualization mode is selected.
	 *
	 * @param InName	The ID of the required visualization mode
	 * @return	true if the supplied Nanite visualization mode is checked
	 */
	bool IsNaniteVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeNanite) && CurrentNaniteVisualizationMode == InName; }

	/**
	 * Returns the FText display name associated with CurrentNaniteVisualizationMode.
	 */
	FText GetCurrentNaniteVisualizationModeDisplayName() const;

	/**
	 * Changes the Lumen visualization mode for this viewport.
	 *
	 * @param InName	The ID of the required visualization mode
	 */
	void ChangeLumenVisualizationMode(FName InName);

	/**
	 * Checks if a Lumen visualization mode is selected.
	 *
	 * @param InName	The ID of the required visualization mode
	 * @return	true if the supplied Lumen visualization mode is checked
	 */
	bool IsLumenVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeLumen) && CurrentLumenVisualizationMode == InName; }

	/**
	 * Returns the FText display name associated with CurrentLumenVisualizationMode.
	 */
	FText GetCurrentLumenVisualizationModeDisplayName() const;

	/**
	 * Changes the Strata visualization mode for this viewport.
	 *
	 * @param InName	The ID of the required visualization mode
	 */
	void ChangeStrataVisualizationMode(FName InName);

	/**
	 * Checks if a Strata visualization mode is selected.
	 *
	 * @param InName	The ID of the required visualization mode
	 * @return	true if the supplied Strata visualization mode is checked
	 */
	bool IsStrataVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeSubstrate) && CurrentStrataVisualizationMode == InName; }

	/**
	 * Returns the FText display name associated with CurrentStrataVisualizationMode.
	 */
	FText GetCurrentStrataVisualizationModeDisplayName() const;

	/**
	 * Changes the Groom visualization mode for this viewport.
	 *
	 * @param InName	The ID of the required visualization mode
	 */
	void ChangeGroomVisualizationMode(FName InName);

	/**
	 * Checks if a Groom visualization mode is selected.
	 *
	 * @param InName	The ID of the required visualization mode
	 * @return	true if the supplied Groom visualization mode is checked
	 */
	bool IsGroomVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeGroom) && CurrentGroomVisualizationMode == InName; }

	/**
	 * Returns the FText display name associated with CurrentGroomVisualizationMode.
	 */
	FText GetCurrentGroomVisualizationModeDisplayName() const;

	/**
	* Changes the virtual shadow map visualization mode for this viewport.
	*
	* @param InName	The ID of the required visualization mode
	*/
	void ChangeVirtualShadowMapVisualizationMode(FName InName);

	/**
	* Checks if a virtual shadow map visualization mode is selected.
	*
	* @param InName	The ID of the required visualization mode
	* @return	true if the supplied virtual shadow map visualization mode is checked
	*/
	bool IsVirtualShadowMapVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeVirtualShadowMap) && CurrentVirtualShadowMapVisualizationMode == InName; }

	/**
	* Returns the FText display name associated with CurrentVirtualShadowMapVisualizationMode.
	*/
	FText GetCurrentVirtualShadowMapVisualizationModeDisplayName() const;

	/**
	* Returns whether visualize debug material is enabled.
	*/
	bool IsVisualizeCalibrationMaterialEnabled() const;

	/**
	 * Changes the ray tracing debug visualization mode for this viewport
	 *
	 * @param InName	The ID of the required ray tracing debug visualization mode
	 */
	void ChangeRayTracingDebugVisualizationMode(FName InName);

	/**
	 * Checks if a ray tracing debug visualization mode is selected
	 *
	 * @param InName	The ID of the required ray tracing debug visualization mode
	 * @return	true if the supplied ray tracing debug visualization mode is checked
	 */
	bool IsRayTracingDebugVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_RayTracingDebug) && CurrentRayTracingDebugVisualizationMode == InName; }

	/**
	 * Changes the GPU Skin Cache visualization mode for this viewport
	 *
	 * @param InName	The ID of the required GPU Skin Cache visualization mode
	 */
	void ChangeGPUSkinCacheVisualizationMode(FName InName);

	/**
	 * Checks if a GPU Skin Cache visualization mode is selected
	 *
	 * @param InName	The ID of the required GPU Skin Cache visualization mode
	 * @return	true if the supplied GPU Skin Cache visualization mode is checked
	 */
	bool IsGPUSkinCacheVisualizationModeSelected(FName InName) const { return IsViewModeEnabled(VMI_VisualizeGPUSkinCache) && CurrentGPUSkinCacheVisualizationMode == InName; }

	/**
	* Returns the FText display name associated with CurrentGPUSkinCacheVisualizationMode.
	*/
	FText GetCurrentGPUSkinCacheVisualizationModeDisplayName() const;

	/** @return True if PreviewResolutionFraction is supported. */
	bool SupportsPreviewResolutionFraction() const;

	/** @return default resolution fraction for UI based on display resolution and user settings. */
	float GetDefaultPrimaryResolutionFractionTarget() const;

	/** @return preview screen percentage for UI. */
	int32 GetPreviewScreenPercentage() const;

	/** Set preview screen percentage on UI behalf. */
	void SetPreviewScreenPercentage(int32 PreviewScreenPercentage);

	/**
	 * Enable customization of the EngineShowFlags for rendering. After calling this function,
	 * the provided OverrideFunc will be passed a copy of .EngineShowFlags in ::Draw() just before rendering setup.
	 * Changes made to the ShowFlags will be used for that frame but .EngineShowFlags will not be modified.
	 * @param OverrideFunc custom override function that will be called every frame until override is disabled.
	 */
	void EnableOverrideEngineShowFlags(TUniqueFunction<void(FEngineShowFlags&)> OverrideFunc) { OverrideShowFlagsFunc = MoveTemp(OverrideFunc); }

	/** Disable EngineShowFlags override if enabled */
	void DisableOverrideEngineShowFlags() { OverrideShowFlagsFunc = nullptr; }

	/** @return true if Override EngineShowFlags are currently enabled */
	bool IsEngineShowFlagsOverrideEnabled() const { return !!OverrideShowFlagsFunc; }

protected:
	/** FCommonViewportClient interface */
	virtual float UpdateViewportClientWindowDPIScale() const override;

private:
	/**
	 * Forcibly disables lighting show flags if there are no lights in the scene, or restores lighting show
	 * flags if lights are added to the scene.
	 */
	void UpdateLightingShowFlags(FEngineShowFlags& InOutShowFlags);

	virtual FEngineShowFlags* GetEngineShowFlags() override { return &EngineShowFlags; }

	/** Delegate handler for when a window DPI changes and we might need to adjust the scenes resolution */
	void HandleWindowDPIScaleChanged(TSharedRef<SWindow> InWindow);

public:
	/** Delegate used to get whether or not this client is in an immersive viewport */
	FCustomViewportStateGetter ImmersiveDelegate;

	/** Delegate used to get the visibility of this client from a slate viewport layout and tab configuration */
	FCustomViewportStateGetter VisibilityDelegate;

	FViewport* Viewport;

	/** Viewport camera transform data for perspective viewports */
	FCustomViewportCameraTransform		ViewTransformPerspective;

	/** Viewport camera transform data for orthographic viewports */
	FCustomViewportCameraTransform		ViewTransformOrthographic;

	/** The viewport type. */
	ECustomViewportType		ViewportType;

	/** The viewport's scene view state. */
	FSceneViewStateReference ViewState;

	/** Viewport view state when stereo rendering is enabled */
	TArray<FSceneViewStateReference> StereoViewStates;

	/** A set of flags that determines visibility for various scene elements. */
	FEngineShowFlags		EngineShowFlags;

	/** Previous value for engine show flags, used for toggling. */
	FEngineShowFlags		LastEngineShowFlags;

	/** Editor setting to allow designers to override the automatic expose */
	FExposureSettings		ExposureSettings;

	FName CurrentBufferVisualizationMode;
	FName CurrentNaniteVisualizationMode;
	FName CurrentLumenVisualizationMode;
	FName CurrentStrataVisualizationMode;
	FName CurrentGroomVisualizationMode;
	FName CurrentVirtualShadowMapVisualizationMode;

	FName CurrentRayTracingDebugVisualizationMode;
	FName CurrentGPUSkinCacheVisualizationMode;

	/** Viewport's current horizontal field of view (can be modified by locked cameras etc.) */
	float ViewFOV;
	/** Viewport's stored horizontal field of view (saved in ini files). */
	float FOVAngle;

	/** true if we've forced the SHOW_Lighting show flag off because there are no lights in the scene */
	bool bForcingUnlitForNewMap;

	/** The number of pending viewport redraws. */
	bool bNeedsRedraw;

	// Override the LOD of landscape in this viewport
	int8 LandscapeLODOverride;

protected:
	/** When we have LOD locking, it's slow to force redraw of other viewports, so we delay invalidates to reduce the number of redraws */
	double TimeForForceRedraw;

	// -1, -1 if not set
	FIntPoint CurrentMousePos;

	/** if the viewport is currently realtime */
	bool bIsRealtime;

	/** The editor viewport widget this client is attached to */
	TWeakPtr<SViewportWidget> ViewportWidget;

	/** The scene used for the viewport. Owned externally */
	FCustomPreviewScene* PreviewScene;

	/** Custom override function that will be called every ::Draw() until override is disabled */
	TUniqueFunction<void(FEngineShowFlags&)> OverrideShowFlagsFunc;

public:
	/* Default view mode for perspective viewports */
	static const EViewModeIndex DefaultPerspectiveViewMode;

	/* Default view mode for orthographic viewports */
	static const EViewModeIndex DefaultOrthoViewMode;

private:
	/** Controles resolution fraction for previewing in editor viewport at different screen percentage. */
	TOptional<float> PreviewResolutionFraction;

	/* View mode to set when this viewport is of type CVT_Perspective */
	EViewModeIndex PerspViewModeIndex;

	/* View mode to set when this viewport is not of type CVT_Perspective */
	EViewModeIndex OrthoViewModeIndex;

	/* View mode param */
	int32 ViewModeParam;
	FName ViewModeParamName;

	/* A map converting the viewmode param into an asset name. The map gets updated while the menu is populated. */
	TMap<int32, FName> ViewModeParamNameMap;

	/** near plane adjustable for each editor view, if < 0 GNearClippingPlane should be used. */
	float NearPlane;

	/** If > 0, overrides the view's far clipping plane with a plane at the specified distance. */
	float FarPlane;

	/** If true, we are in Game View mode*/
	bool bInGameViewMode;
};