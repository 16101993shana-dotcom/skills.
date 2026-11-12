// Automation tests for the heightfield. Run from Tools > Session Frontend >
// Automation, filter "Chronopolis.Terrain", or from the command line.

#include "ChronoHeightfield.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoHeightfieldTests
{
	/** A small heightfield with a deterministic, non-flat relief. */
	FChronoHeightfield MakeRelief()
	{
		FChronoHeightfield Heightfield;
		Heightfield.Init(64, 200.0f, 0.0f);
		for (int32 Y = 0; Y <= 64; ++Y)
		{
			for (int32 X = 0; X <= 64; ++X)
			{
				Heightfield.SetVertexHeight(X, Y, 300.0f * FMath::Sin(X * 0.37f) + 200.0f * FMath::Cos(Y * 0.53f));
			}
		}
		return Heightfield;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoHeightfieldSampleTest, "Chronopolis.Terrain.Heightfield.SampleMatchesVertices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoHeightfieldSampleTest::RunTest(const FString& Parameters)
{
	const FChronoHeightfield Heightfield = ChronoHeightfieldTests::MakeRelief();

	// Sampling exactly on a vertex returns the vertex height.
	for (int32 Y = 0; Y <= 64; Y += 7)
	{
		for (int32 X = 0; X <= 64; X += 5)
		{
			const float Sampled = Heightfield.SampleHeight(Heightfield.VertexToWorld(X, Y));
			TestTrue(TEXT("Sample equals vertex height"), FMath::IsNearlyEqual(Sampled, Heightfield.GetVertexHeight(X, Y), 0.01f));
		}
	}

	// On the shared diagonal both triangles must agree (no crack, no step).
	const FVector2D Corner = Heightfield.VertexToWorld(10, 10);
	const float Expected = 0.5f * (Heightfield.GetVertexHeight(10, 10) + Heightfield.GetVertexHeight(11, 11));
	const float OnDiagonal = Heightfield.SampleHeight(Corner + FVector2D(100.0, 100.0));
	TestTrue(TEXT("Diagonal midpoint is the average of its two corners"), FMath::IsNearlyEqual(OnDiagonal, Expected, 0.01f));

	// Outside the world the height is clamped, never garbage.
	const float Outside = Heightfield.SampleHeight(FVector2D(1.0e7, 1.0e7));
	TestTrue(TEXT("Out of bounds sample is clamped to the border"), FMath::IsNearlyEqual(Outside, Heightfield.GetVertexHeight(64, 64), 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoHeightfieldFlattenTest, "Chronopolis.Terrain.Heightfield.FlattenFootprintIsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoHeightfieldFlattenTest::RunTest(const FString& Parameters)
{
	FChronoHeightfield Heightfield = ChronoHeightfieldTests::MakeRelief();

	// A rotated footprint that is not aligned with the grid.
	const FVector2D Center(350.0, -420.0);
	const FVector2D HalfExtent(1300.0, 700.0);
	const float Yaw = FMath::DegreesToRadians(33.0f);
	const float Target = 125.0f;

	const FIntRect Dirty = Heightfield.FlattenOrientedBox(Center, HalfExtent, Yaw, Target, 800.0f);
	TestTrue(TEXT("Flatten reports a dirty rectangle"), Dirty.Width() > 0 && Dirty.Height() > 0);

	// Zero-clipping contract: the ground is exactly at the target everywhere
	// under the footprint, including on its border.
	const FVector2D AxisX(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D AxisY(-FMath::Sin(Yaw), FMath::Cos(Yaw));
	float MaxError = 0.0f;
	for (int32 StepY = -20; StepY <= 20; ++StepY)
	{
		for (int32 StepX = -20; StepX <= 20; ++StepX)
		{
			const FVector2D Point = Center + AxisX * (HalfExtent.X * StepX / 20.0) + AxisY * (HalfExtent.Y * StepY / 20.0);
			MaxError = FMath::Max(MaxError, FMath::Abs(Heightfield.SampleHeight(Point) - Target));
		}
	}
	TestTrue(TEXT("Ground is flat under the whole footprint"), MaxError < 0.01f);

	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	Heightfield.GetHeightRangeInOrientedBox(Center, HalfExtent * 0.9, Yaw, MinHeight, MaxHeight);
	TestTrue(TEXT("Height range under the footprint collapses to the target"), FMath::IsNearlyEqual(MinHeight, Target, 0.01f) && FMath::IsNearlyEqual(MaxHeight, Target, 0.01f));

	// Far from the footprint the relief is untouched.
	const FChronoHeightfield Reference = ChronoHeightfieldTests::MakeRelief();
	TestTrue(TEXT("Terrain far from the footprint is unchanged"), FMath::IsNearlyEqual(Heightfield.GetVertexHeight(2, 62), Reference.GetVertexHeight(2, 62), 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoHeightfieldSerializeTest, "Chronopolis.Terrain.Heightfield.SerializeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoHeightfieldSerializeTest::RunTest(const FString& Parameters)
{
	FChronoHeightfield Source = ChronoHeightfieldTests::MakeRelief();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Source.Serialize(Writer);

	FChronoHeightfield Loaded;
	FMemoryReader Reader(Bytes);
	Loaded.Serialize(Reader);

	TestFalse(TEXT("Reader has no error"), Reader.IsError());
	TestTrue(TEXT("Loaded heightfield is valid"), Loaded.IsValid());
	TestEqual(TEXT("Size is preserved"), Loaded.GetSizeCells(), Source.GetSizeCells());
	TestTrue(TEXT("Heights are preserved"), Loaded.GetHeights() == Source.GetHeights());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
