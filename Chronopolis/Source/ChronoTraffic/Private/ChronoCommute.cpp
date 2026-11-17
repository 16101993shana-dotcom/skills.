#include "ChronoCommute.h"

#include "ChronoBuildingSet.h"
#include "ChronoPopulation.h"
#include "ChronoTrafficSettings.h"

namespace ChronoCommutePrivate
{
	/** A number in [0, 1) that depends only on the citizen and on Salt. */
	double UnitHash(int32 CitizenId, uint32 Salt)
	{
		uint32 Hash = static_cast<uint32>(CitizenId) * 2654435761u + Salt * 40503u;
		Hash ^= Hash >> 15;
		Hash *= 2246822519u;
		Hash ^= Hash >> 13;
		return (Hash & 0xFFFFFF) / 16777216.0;
	}

	/** Where a building meets its road. */
	bool GetAccess(const FChronoBuildingSet& Buildings, int32 BuildingId, double CellSizeCm, int32& OutSegmentId, double& OutSegmentDistance, int32& OutSide, FVector2D& OutCenter)
	{
		const FChronoBuilding* Building = Buildings.FindBuilding(BuildingId);
		if (!Building)
		{
			return false;
		}

		// Slots are counted from the start of the segment: the door is in the middle of the lot.
		OutSegmentId = Building->SegmentId;
		OutSegmentDistance = (Building->FirstSlot + 0.5 * Building->WidthCells) * CellSizeCm;
		OutSide = Building->Side;
		OutCenter = Building->Footprint.Center;
		return true;
	}
}

FChronoCommuteRules FChronoCommuteRules::FromSettings()
{
	const UChronoTrafficSettings* Settings = UChronoTrafficSettings::Get();

	FChronoCommuteRules Rules;
	Rules.SampleRate = Settings->TripSampleRate;
	Rules.ComfortableCommuteSeconds = Settings->ComfortableCommuteSeconds;
	Rules.HappinessLostPerExtraMinute = Settings->HappinessLostPerExtraMinute;
	Rules.MaxHappinessLoss = Settings->MaxCommuteHappinessLoss;
	Rules.WalkDistanceCm = Settings->WalkDistanceCm;
	return Rules;
}

void ChronoCommute::GenerateTrips(const FChronoPopulation& Population, const FChronoBuildingSet& Buildings, double CellSizeCm, double FromDayFraction, double ToDayFraction, const FChronoCommuteRules& Rules, TArray<FChronoTripRequest>& OutTrips)
{
	using namespace ChronoCommutePrivate;

	const TArray<FChronoCitizen>& Citizens = Population.GetCitizens();
	const TArray<FChronoHousehold>& Households = Population.GetHouseholds();

	for (int32 CitizenId = 0; CitizenId < Citizens.Num(); ++CitizenId)
	{
		const FChronoCitizen& Citizen = Citizens[CitizenId];
		if (Citizen.HouseholdId == INDEX_NONE || Citizen.WorkBuildingId == INDEX_NONE)
		{
			continue;
		}

		// With a sample rate below 1, the same citizens drive every day.
		if (UnitHash(CitizenId, 3) >= Rules.SampleRate)
		{
			continue;
		}

		// Each commuter has their own habits: the same times every day.
		const double Morning = Rules.MorningStart + UnitHash(CitizenId, 1) * (Rules.MorningEnd - Rules.MorningStart);
		const double Evening = Rules.EveningStart + UnitHash(CitizenId, 2) * (Rules.EveningEnd - Rules.EveningStart);
		const bool bLeavesHome = Morning >= FromDayFraction && Morning < ToDayFraction;
		const bool bLeavesWork = Evening >= FromDayFraction && Evening < ToDayFraction;
		if (!bLeavesHome && !bLeavesWork)
		{
			continue;
		}

		int32 HomeSegment = INDEX_NONE;
		int32 WorkSegment = INDEX_NONE;
		double HomeDistance = 0.0;
		double WorkDistance = 0.0;
		int32 HomeSide = 1;
		int32 WorkSide = 1;
		FVector2D HomeCenter;
		FVector2D WorkCenter;
		if (!GetAccess(Buildings, Households[Citizen.HouseholdId].HomeBuildingId, CellSizeCm, HomeSegment, HomeDistance, HomeSide, HomeCenter)
			|| !GetAccess(Buildings, Citizen.WorkBuildingId, CellSizeCm, WorkSegment, WorkDistance, WorkSide, WorkCenter))
		{
			continue;
		}

		FChronoTripRequest Trip;
		Trip.UserId = CitizenId;

		// Work close to home is walked to, the rest is driven to.
		Trip.bWalk = FVector2D::Distance(HomeCenter, WorkCenter) <= Rules.WalkDistanceCm;
		if (bLeavesHome)
		{
			Trip.FromSegmentId = HomeSegment;
			Trip.FromSegmentDistance = HomeDistance;
			Trip.FromSide = HomeSide;
			Trip.ToSegmentId = WorkSegment;
			Trip.ToSegmentDistance = WorkDistance;
			OutTrips.Add(Trip);
		}
		if (bLeavesWork)
		{
			Trip.FromSegmentId = WorkSegment;
			Trip.FromSegmentDistance = WorkDistance;
			Trip.FromSide = WorkSide;
			Trip.ToSegmentId = HomeSegment;
			Trip.ToSegmentDistance = HomeDistance;
			OutTrips.Add(Trip);
		}
	}
}

float ChronoCommute::ComputeCommuteHappiness(float AverageTripSeconds, const FChronoCommuteRules& Rules)
{
	const float ExtraMinutes = FMath::Max(AverageTripSeconds - Rules.ComfortableCommuteSeconds, 0.0f) / 60.0f;
	return -FMath::Min(ExtraMinutes * Rules.HappinessLostPerExtraMinute, Rules.MaxHappinessLoss);
}
