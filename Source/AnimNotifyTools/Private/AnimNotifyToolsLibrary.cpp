#include "AnimNotifyToolsLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "UObject/Package.h"
#include "FileHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimNotifyTools, Log, All);

namespace AnimNotifyToolsPrivate
{
	// A cheap "identity" for a notify event: what kind of notify it is, independent of time.
	// Two events with the same key are considered the "same notify" for missing/duplicate purposes.
	static FString GetNotifyIdentityKey(const FAnimNotifyEvent& Event)
	{
		if (Event.NotifyStateClass)
		{
			return FString::Printf(TEXT("STATE:%s"), *Event.NotifyStateClass->GetClass()->GetName());
		}
		if (Event.Notify)
		{
			return FString::Printf(TEXT("NOTIFY:%s"), *Event.Notify->GetClass()->GetName());
		}
		// "Simple" notify - identified purely by its name (no notify object assigned).
		return FString::Printf(TEXT("SIMPLE:%s"), *Event.NotifyName.ToString());
	}

	static FString GetNotifyDisplayName(const FAnimNotifyEvent& Event)
	{
		if (Event.NotifyStateClass)
		{
			return Event.NotifyStateClass->GetClass()->GetDisplayNameText().ToString();
		}
		if (Event.Notify)
		{
			return Event.Notify->GetClass()->GetDisplayNameText().ToString();
		}
		return Event.NotifyName.ToString();
	}

	// Does TargetMontage already have a notify that matches SourceEvent's identity within TimeTolerance?
	static bool HasEquivalentNotify(const UAnimMontage* TargetMontage, const FAnimNotifyEvent& SourceEvent, float SourceTime, float TimeTolerance)
	{
		const FString SourceKey = GetNotifyIdentityKey(SourceEvent);

		for (const FAnimNotifyEvent& TargetEvent : TargetMontage->Notifies)
		{
			if (GetNotifyIdentityKey(TargetEvent) != SourceKey)
			{
				continue;
			}

			const float TargetTime = TargetEvent.GetTime();
			if (FMath::Abs(TargetTime - SourceTime) <= TimeTolerance)
			{
				return true;
			}
		}
		return false;
	}

	// Finds the track on Target with the given name, creating it (with the given color) if it
	// doesn't exist yet. This is how we make sure a copied notify lands on the same *named* row
	// in Persona, not just whatever index happened to be free.
	static int32 FindOrAddTrackByName(UAnimMontage* Target, FName TrackName, FLinearColor TrackColor)
	{
		for (int32 i = 0; i < Target->AnimNotifyTracks.Num(); ++i)
		{
			if (Target->AnimNotifyTracks[i].TrackName == TrackName)
			{
				return i;
			}
		}

		FAnimNotifyTrack NewTrack(TrackName, TrackColor);
		Target->AnimNotifyTracks.Add(NewTrack);
		return Target->AnimNotifyTracks.Num() - 1;
	}
}

int32 UAnimNotifyToolsLibrary::CopyMissingAnimNotifies(
	UAnimMontage* SourceMontage,
	UAnimMontage* TargetMontage,
	float TimeTolerance,
	bool bSaveTargetAsset,
	TArray<FString>& OutAddedNotifyDescriptions,
	TArray<FString>& OutSkippedNotifyDescriptions)
{
	using namespace AnimNotifyToolsPrivate;

	OutAddedNotifyDescriptions.Reset();
	OutSkippedNotifyDescriptions.Reset();

	if (!SourceMontage || !TargetMontage)
	{
		UE_LOG(LogAnimNotifyTools, Warning, TEXT("CopyMissingAnimNotifies: Source and Target montages must both be set."));
		return 0;
	}

	if (SourceMontage == TargetMontage)
	{
		UE_LOG(LogAnimNotifyTools, Warning, TEXT("CopyMissingAnimNotifies: Source and Target are the same montage, nothing to do."));
		return 0;
	}

	if (TimeTolerance < 0.f)
	{
		TimeTolerance = 0.f;
	}

	const float TargetPlayLength = TargetMontage->GetPlayLength();
	const int32 TargetSlotCount = TargetMontage->SlotAnimTracks.Num();

	TargetMontage->Modify();

	int32 NumAdded = 0;

	// Snapshot source times up front - GetTime() re-caches internally and we don't want that
	// interacting with anything we do to TargetMontage while iterating.
	struct FPendingCopy
	{
		const FAnimNotifyEvent* SourceEvent;
		float SourceTime;
	};

	TArray<FPendingCopy> PendingCopies;
	PendingCopies.Reserve(SourceMontage->Notifies.Num());

	for (const FAnimNotifyEvent& SourceEvent : SourceMontage->Notifies)
	{
		const float SourceTime = SourceEvent.GetTime();

		if (HasEquivalentNotify(TargetMontage, SourceEvent, SourceTime, TimeTolerance))
		{
			OutSkippedNotifyDescriptions.Add(FString::Printf(TEXT("%s @ %.3fs (already present)"), *GetNotifyDisplayName(SourceEvent), SourceTime));
			continue;
		}

		PendingCopies.Add({ &SourceEvent, SourceTime });
	}

	for (const FPendingCopy& Pending : PendingCopies)
	{
		const FAnimNotifyEvent& SourceEvent = *Pending.SourceEvent;

		// Clamp into the target montage's length rather than silently failing on
		// out-of-range times (e.g. source notify sits after target's shorter length).
		float NewTime = Pending.SourceTime;
		bool bWasClamped = false;
		if (NewTime > TargetPlayLength)
		{
			NewTime = FMath::Max(TargetPlayLength - KINDA_SMALL_NUMBER, 0.f);
			bWasClamped = true;
		}
		else if (NewTime < 0.f)
		{
			NewTime = 0.f;
			bWasClamped = true;
		}

		FAnimNotifyEvent NewEvent;
		NewEvent.NotifyName = SourceEvent.NotifyName;
		NewEvent.TriggerWeightThreshold = SourceEvent.TriggerWeightThreshold;
		NewEvent.NotifyTriggerChance = SourceEvent.NotifyTriggerChance;
		NewEvent.NotifyFilterType = SourceEvent.NotifyFilterType;
		NewEvent.NotifyFilterLOD = SourceEvent.NotifyFilterLOD;
		NewEvent.MontageTickType = SourceEvent.MontageTickType;
		NewEvent.bTriggerOnDedicatedServer = SourceEvent.bTriggerOnDedicatedServer;
		NewEvent.bTriggerOnFollower = SourceEvent.bTriggerOnFollower;

		// Duplicate the notify object(s) so the target owns its own instance rather than
		// sharing one with the source montage.
		if (SourceEvent.Notify)
		{
			NewEvent.Notify = DuplicateObject<UAnimNotify>(SourceEvent.Notify, TargetMontage);
		}
		if (SourceEvent.NotifyStateClass)
		{
			NewEvent.NotifyStateClass = DuplicateObject<UAnimNotifyState>(SourceEvent.NotifyStateClass, TargetMontage);
		}

		// Pick a slot that exists on the target; fall back to slot 0 if the montage layouts differ.
		const int32 SafeSlotIndex = (TargetSlotCount > 0) ? FMath::Clamp(SourceEvent.GetSlotIndex(), 0, TargetSlotCount - 1) : 0;

		// Link establishes the segment/slot this notify belongs to and caches its time -
		// this is the C++-only step that Blueprint/Python cannot perform.
		NewEvent.Link(TargetMontage, NewTime, SafeSlotIndex);
		NewEvent.SetTime(NewTime);

		if (SourceEvent.NotifyStateClass)
		{
			NewEvent.SetDuration(SourceEvent.GetDuration());
		}

		// Put this notify on the target track with the SAME NAME as its source track,
		// creating that track on the target if it doesn't exist yet. Falls back to track 0
		// only if the source track index itself is somehow invalid.
		int32 NewTrackIndex = 0;
		if (SourceMontage->AnimNotifyTracks.IsValidIndex(SourceEvent.TrackIndex))
		{
			const FAnimNotifyTrack& SourceTrack = SourceMontage->AnimNotifyTracks[SourceEvent.TrackIndex];
			NewTrackIndex = FindOrAddTrackByName(TargetMontage, SourceTrack.TrackName, SourceTrack.TrackColor);
		}
		NewEvent.TrackIndex = NewTrackIndex;

		TargetMontage->Notifies.Add(NewEvent);
		++NumAdded;

		FString Description = FString::Printf(TEXT("%s @ %.3fs"), *GetNotifyDisplayName(SourceEvent), NewTime);
		if (bWasClamped)
		{
			Description += TEXT(" (clamped to fit target length)");
		}
		OutAddedNotifyDescriptions.Add(Description);
	}

	if (NumAdded > 0)
	{
		// Keep the notify list in time order, which is what Persona expects.
		TargetMontage->Notifies.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B)
		{
			return A.GetTime() < B.GetTime();
		});

		TargetMontage->PostEditChange();
		TargetMontage->MarkPackageDirty();

		UE_LOG(LogAnimNotifyTools, Log, TEXT("CopyMissingAnimNotifies: Added %d notify event(s) from '%s' to '%s'."),
			NumAdded, *SourceMontage->GetName(), *TargetMontage->GetName());

		if (bSaveTargetAsset)
		{
			UPackage* Package = TargetMontage->GetOutermost();
			if (Package)
			{
				FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty=*/ false, /*bPromptToSave=*/ false);
			}
		}
	}
	else
	{
		UE_LOG(LogAnimNotifyTools, Log, TEXT("CopyMissingAnimNotifies: Nothing to copy, '%s' already has every notify from '%s'."),
			*TargetMontage->GetName(), *SourceMontage->GetName());
	}

	return NumAdded;
}
