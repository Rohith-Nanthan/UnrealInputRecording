// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/InputIconMapping.h"

#include "Engine/Texture2D.h"

bool UInputIconMapping::FindEntry(FName ActionName, FInputIconEntry& OutEntry) const
{
	for (const FInputIconEntry& Entry : Entries)
	{
		if (Entry.ActionName == ActionName)
		{
			OutEntry = Entry;
			return true;
		}
	}

	return false;
}

UTexture2D* UInputIconMapping::FindIcon(FName ActionName) const
{
	FInputIconEntry Entry;
	if (!FindEntry(ActionName, Entry))
	{
		return nullptr;
	}

	return Entry.Icon.LoadSynchronous();
}
