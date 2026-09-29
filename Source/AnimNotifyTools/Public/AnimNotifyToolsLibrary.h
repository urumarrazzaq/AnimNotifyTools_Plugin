#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimNotifyToolsLibrary.generated.h"

class UAnimMontage;

/**
 * Editor-only Blueprint function library for working with AnimNotify / AnimNotifyState
 * events on Anim Montages. Designed to be called from an Editor Utility Widget.
 *
 * Why this needs to be C++:
 * FAnimNotifyEvent's actual timing (Time / TriggerTimeOffset / Duration / segment link)
 * is NOT exposed as a Blueprint- or Python-readable/writable property - it can only be
 * read or written through the C++-only GetTime() / SetTime() / Link() / SetDuration()
 * accessors on FAnimLinkableElement / FAnimNotifyEvent. A pure Blueprint or Python
 * implementation cannot correctly reposition a notify in time.
 */
UCLASS()
class ANIMNOTIFYTOOLS_API UAnimNotifyToolsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Copies every AnimNotify / AnimNotifyState event that exists on SourceMontage but is
	 * missing on TargetMontage, at the same point in time. "Missing" means: no notify of
	 * the same identity (same simple name, or same Notify/NotifyState class) already sits
	 * within TimeTolerance seconds of that time on the target.
	 *
	 * Existing notifies already on the target are left completely untouched.
	 *
	 * @param SourceMontage      Montage to read notifies from.
	 * @param TargetMontage      Montage to add missing notifies to. Modified in place and marked dirty.
	 * @param TimeTolerance      Two notifies within this many seconds of each other (and with the
	 *                           same identity) are considered "the same" and will not be duplicated.
	 * @param bSaveTargetAsset   If true, saves TargetMontage's package to disk after copying.
	 * @param OutAddedNotifyDescriptions   Human readable list of what was added, in "Name @ Time" form.
	 * @param OutSkippedNotifyDescriptions Human readable list of what was already present and skipped.
	 * @return Number of notify events actually added to TargetMontage.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Notify Tools", meta = (DisplayName = "Copy Missing Anim Notifies"))
	static int32 CopyMissingAnimNotifies(
		UAnimMontage* SourceMontage,
		UAnimMontage* TargetMontage,
		float TimeTolerance,
		bool bSaveTargetAsset,
		TArray<FString>& OutAddedNotifyDescriptions,
		TArray<FString>& OutSkippedNotifyDescriptions);
};
