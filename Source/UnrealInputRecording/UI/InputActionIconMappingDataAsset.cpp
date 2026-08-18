// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/InputActionIconMappingDataAsset.h"

#include "InputAction.h"
#include "InputReplay/InputMatchCue.h"

void UInputActionIconMappingDataAsset::BuildLookupIfNeeded() const
{
	if (bLookupBuilt)
	{
		return;
	}

	PathToEntry.Reset();
	NameToEntry.Reset();

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FInputActionIconEntry& Entry = Entries[Index];

		if (!Entry.Action.IsNull())
		{
			// ToSoftObjectPath() rather than resolving: we want the key, not the asset. Nothing is
			// loaded by building this map.
			PathToEntry.Add(Entry.Action.ToSoftObjectPath(), Index);
		}

		// An explicit ActionName wins over one derived from the path, so a designer can point two
		// differently-named actions at the same icon without duplicating brushes.
		if (!Entry.ActionName.IsNone())
		{
			NameToEntry.Add(Entry.ActionName, Index);
		}
		else if (!Entry.Action.IsNull())
		{
			NameToEntry.Add(FName(*Entry.Action.GetAssetName()), Index);
		}
	}

	bLookupBuilt = true;
}

void UInputActionIconMappingDataAsset::InvalidateLookup()
{
	bLookupBuilt = false;
	PathToEntry.Reset();
	NameToEntry.Reset();
}

int32 UInputActionIconMappingDataAsset::FindEntryIndexByPath(const FSoftObjectPath& Path) const
{
	if (Path.IsNull())
	{
		return INDEX_NONE;
	}

	BuildLookupIfNeeded();

	const int32* Found = PathToEntry.Find(Path);
	return Found ? *Found : INDEX_NONE;
}

int32 UInputActionIconMappingDataAsset::FindEntryIndexByName(FName ActionName) const
{
	if (ActionName.IsNone())
	{
		return INDEX_NONE;
	}

	BuildLookupIfNeeded();

	const int32* Found = NameToEntry.Find(ActionName);
	return Found ? *Found : INDEX_NONE;
}

FSlateBrush UInputActionIconMappingDataAsset::GetIconForAction(const UInputAction* Action) const
{
	if (!Action)
	{
		return DefaultIcon;
	}

	int32 Index = FindEntryIndexByPath(FSoftObjectPath(Action));
	if (Index == INDEX_NONE)
	{
		Index = FindEntryIndexByName(Action->GetFName());
	}

	return Entries.IsValidIndex(Index) ? Entries[Index].Icon : DefaultIcon;
}

FSlateBrush UInputActionIconMappingDataAsset::GetIconForSoftAction(const TSoftObjectPtr<UInputAction>& Action) const
{
	if (Action.IsNull())
	{
		return DefaultIcon;
	}

	int32 Index = FindEntryIndexByPath(Action.ToSoftObjectPath());
	if (Index == INDEX_NONE)
	{
		Index = FindEntryIndexByName(FName(*Action.GetAssetName()));
	}

	return Entries.IsValidIndex(Index) ? Entries[Index].Icon : DefaultIcon;
}

FSlateBrush UInputActionIconMappingDataAsset::GetIconForActionName(FName ActionName) const
{
	const int32 Index = FindEntryIndexByName(ActionName);
	return Entries.IsValidIndex(Index) ? Entries[Index].Icon : DefaultIcon;
}

FSlateBrush UInputActionIconMappingDataAsset::GetIconForCue(const FMatchInputCue& Cue) const
{
	// Path first: it is the precise key, and two actions can legitimately share a short name across
	// folders. Name second: it is the key that survives the action asset being moved or renamed after
	// the recording was made.
	int32 Index = FindEntryIndexByPath(Cue.Action.ToSoftObjectPath());
	if (Index == INDEX_NONE)
	{
		Index = FindEntryIndexByName(FName(*Cue.ActionName));
	}

	return Entries.IsValidIndex(Index) ? Entries[Index].Icon : DefaultIcon;
}

FText UInputActionIconMappingDataAsset::GetDisplayNameForCue(const FMatchInputCue& Cue) const
{
	int32 Index = FindEntryIndexByPath(Cue.Action.ToSoftObjectPath());
	if (Index == INDEX_NONE)
	{
		Index = FindEntryIndexByName(FName(*Cue.ActionName));
	}

	if (Entries.IsValidIndex(Index) && !Entries[Index].DisplayName.IsEmpty())
	{
		return Entries[Index].DisplayName;
	}

	return FText::FromString(Cue.ActionName);
}

FText UInputActionIconMappingDataAsset::GetDisplayNameForActionName(FName ActionName) const
{
	const int32 Index = FindEntryIndexByName(ActionName);

	if (Entries.IsValidIndex(Index) && !Entries[Index].DisplayName.IsEmpty())
	{
		return Entries[Index].DisplayName;
	}

	return FText::FromName(ActionName);
}

bool UInputActionIconMappingDataAsset::HasEntryForActionName(FName ActionName) const
{
	return FindEntryIndexByName(ActionName) != INDEX_NONE;
}

#if WITH_EDITOR
void UInputActionIconMappingDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Fill the fallback key in for the designer. Doing it here rather than at lookup time means what
	// you see in the details panel is what the lookup will actually use.
	for (FInputActionIconEntry& Entry : Entries)
	{
		if (Entry.ActionName.IsNone() && !Entry.Action.IsNull())
		{
			Entry.ActionName = FName(*Entry.Action.GetAssetName());
		}
	}

	InvalidateLookup();
}
#endif
