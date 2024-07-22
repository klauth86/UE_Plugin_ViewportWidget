// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "Components/Widget.h"
#include "Widgets/SViewportWidget.h"
#include "ViewportWidget.generated.h"

//------------------------------------------------------
// UViewportWidget
//------------------------------------------------------

UCLASS()
class VIEWPORTWIDGET_API UViewportWidget : public UWidget
{
	GENERATED_BODY()

public:
	//~ UWidget interface
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End of UVisual interface

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	UFUNCTION(BlueprintCallable, Category="ViewportWidget")
	FTransform GetViewTransform() const { return ViewTransform; }

	UFUNCTION(BlueprintCallable, Category = "ViewportWidget")
	void SetViewTransform(FTransform viewTransform);

	UFUNCTION(BlueprintCallable, Category = "ViewportWidget")
	const TArray<FViewportWidgetEntry>& GetEntries() const { return Entries; }

	UFUNCTION(BlueprintCallable, Category = "ViewportWidget")
	void SetEntries(const TArray<FViewportWidgetEntry>& entries);

	UFUNCTION(BlueprintCallable, Category = "ViewportWidget")
	AActor* GetSpawnedActor(const int32 entryIndex) const;

protected:
	//~ UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	//~ End of UWidget interface

protected:
	TSharedPtr<SViewportWidget> MyViewportWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewportWidget")
	FTransform ViewTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewportWidget")
	TArray<FViewportWidgetEntry> Entries;
};