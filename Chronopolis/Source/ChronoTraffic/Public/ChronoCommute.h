// Who is on the road, and when: every employed citizen drives to work in the
// morning and back home in the evening of each traffic day. Plain functions,
// with no state: the same population always gives the same trips.

#pragma once

#include "CoreMinimal.h"
#include "ChronoTrafficSim.h"

class FChronoBuildingSet;
class FChronoPopulation;

struct CHRONOTRAFFIC_API FChronoCommuteRules
{
	// Departure windows, as fractions of the traffic day.
	float MorningStart = 0.20f;
	float MorningEnd = 0.35f;
	float EveningStart = 0.65f;
	float EveningEnd = 0.80f;

	/** Share of the commuters who travel as visible agents. */
	float SampleRate = 1.0f;

	/** A commute between two buildings closer than this, as the crow flies, is walked. */
	float WalkDistanceCm = 30000.0f;

	// Happiness.
	float ComfortableCommuteSeconds = 60.0f;
	float HappinessLostPerExtraMinute = 5.0f;
	float MaxHappinessLoss = 20.0f;

	static FChronoCommuteRules FromSettings();
};

namespace ChronoCommute
{
	/**
	 * Trips that start between two moments of the traffic day (fractions in
	 * [0, 1], FromDayFraction included, ToDayFraction excluded).
	 * @param CellSizeCm size of a zoning cell, to locate each building along its road.
	 */
	CHRONOTRAFFIC_API void GenerateTrips(const FChronoPopulation& Population, const FChronoBuildingSet& Buildings, double CellSizeCm, double FromDayFraction, double ToDayFraction, const FChronoCommuteRules& Rules, TArray<FChronoTripRequest>& OutTrips);

	/** Happiness change of commuters (0 or negative) for an average commute time. */
	CHRONOTRAFFIC_API float ComputeCommuteHappiness(float AverageTripSeconds, const FChronoCommuteRules& Rules);
}
