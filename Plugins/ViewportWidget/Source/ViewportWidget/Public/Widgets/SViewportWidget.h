// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "Widgets/SViewport.h"
#include "ViewportWidgetEntry.h"

class FCustomViewportClient;
class FCustomPreviewScene;

//------------------------------------------------------
// SViewportWidget
//------------------------------------------------------

class VIEWPORTWIDGET_API SViewportWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SViewportWidget) :_ViewportSize(SViewport::FArguments::GetDefaultViewportSize()), _ViewTransform(FTransform::Identity), _Entries(FViewportWidgetEntry::GetEmptyCollection()) {}
	SLATE_ATTRIBUTE(FVector2D, ViewportSize);
	SLATE_ATTRIBUTE(FTransform, ViewTransform);
	SLATE_ATTRIBUTE(TArray<FViewportWidgetEntry>, Entries);
	SLATE_END_ARGS()

	SViewportWidget();
	virtual ~SViewportWidget();

	void Construct(const FArguments& InArgs);

	void SetViewTransform(const FTransform& viewTransform);

	void SetEntries(TArray<FViewportWidgetEntry>& entries);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** @return True if the viewport is currently visible */
	virtual bool IsVisible() const;

	TSharedPtr<FCustomViewportClient> GetViewportClient() const { return Client; }

	/**
	 * @return The current FSceneViewport shared pointer
	 */
	TSharedPtr<FSceneViewport> GetSceneViewport() { return SceneViewport; }

protected:
	virtual TSharedRef<FCustomViewportClient> MakeViewportClient();

	void CleanEntries();
	void AddEntries();

	virtual void SetupSpawnedActor(AActor* actor, UWorld* world) {}

protected:
	/** Viewport that renders the scene provided by the viewport client */
	TSharedPtr<FSceneViewport> SceneViewport;

	/** Widget where the scene viewport is drawn in */
	TSharedPtr<SViewport> ViewportWidget;

	/** The client responsible for setting up the scene */
	TSharedPtr<FCustomViewportClient> Client;

	/** The last time the viewport was ticked (for visibility determination) */
	double LastTickTime;

	TSharedPtr<FCustomPreviewScene> PreviewScene;

	TAttribute<FTransform> ViewTransform;

	TAttribute<TArray<FViewportWidgetEntry>> Entries;
};