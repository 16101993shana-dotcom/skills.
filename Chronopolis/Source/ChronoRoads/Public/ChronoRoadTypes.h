// Data types shared by the road systems.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ChronoRoadTypes.generated.h"

/**
 * Cross-section and limits of one kind of road. Authored in a UChronoRoadProfile
 * data asset (one per road type and era), then copied into each segment so the
 * road graph stays plain data.
 */
USTRUCT(BlueprintType)
struct CHRONOROADS_API FChronoRoadProfileData
{
	GENERATED_BODY()

	/** Stable identifier used by save games. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FName ProfileId = NAME_None;

	/** Width of the part used by vehicles (both directions), in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "100.0"))
	float CarriagewayWidthCm = 700.0f;

	/** Width of each sidewalk. 0 for a road without sidewalks (dirt path). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "0.0"))
	float SidewalkWidthCm = 200.0f;

	/** Height of the sidewalk above the carriageway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "0.0"))
	float CurbHeightCm = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "1"))
	int32 LanesPerDirection = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	bool bOneWay = false;

	/** Steepest allowed grade, in percent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "0.5"))
	float MaxSlopePercent = 10.0f;

	/** Tightest allowed curve radius, measured on the centerline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road", meta = (ClampMin = "100.0"))
	float MinCurveRadiusCm = 1500.0f;

	/** Distance from the centerline to the outer edge. */
	float GetHalfWidthCm() const { return 0.5f * CarriagewayWidthCm + FMath::Max(SidewalkWidthCm, 0.0f); }

	bool HasSidewalk() const { return SidewalkWidthCm > 0.0f; }
};

UCLASS(BlueprintType)
class CHRONOROADS_API UChronoRoadProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Name shown in the build menu (from a String Table). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Road")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Road")
	FChronoRoadProfileData Data;
};

/** Outcome of a road placement. Every failure has a player-facing message. */
UENUM(BlueprintType)
enum class EChronoRoadBuildResult : uint8
{
	Success,
	InvalidProfile,
	OutOfBounds,
	SameEndpoint,
	TooShort,
	TooSteep,
	CurveTooTight,
	AngleTooSharp,
	TooManyRoads,
	OverlapsRoad
};

USTRUCT(BlueprintType)
struct CHRONOROADS_API FChronoRoadBuildRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FVector2D Start = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FVector2D End = FVector2D::ZeroVector;

	/** When true, the road bends toward Control (quadratic curve). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	bool bCurved = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FVector2D Control = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FChronoRoadProfileData Profile;
};

/** What changed in the road graph after one edit. */
struct FChronoRoadChangeSet
{
	TArray<int32> ChangedSegments;
	TArray<int32> RemovedSegments;
	TArray<int32> ChangedNodes;
	TArray<int32> RemovedNodes;

	bool IsEmpty() const
	{
		return ChangedSegments.Num() == 0 && RemovedSegments.Num() == 0 && ChangedNodes.Num() == 0 && RemovedNodes.Num() == 0;
	}
};
