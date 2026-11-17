// Runs the zero-clipping audit on the live world and shows the result.
// Console: "Chrono.Audit.Run" runs it, logs every issue and marks each one in
// the world; "Chrono.Audit.Clear" removes the markers.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoClippingAudit.h"
#include "ChronoAuditSubsystem.generated.h"

UCLASS()
class CHRONOAUDIT_API UChronoAuditSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Runs the audit on the current terrain and roads. Returns the number of issues. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Audit")
	int32 RunAudit();

	/** Number of issues found by the last run. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Audit")
	int32 GetNumIssues() const { return Issues.Num(); }

	/** Text of one issue of the last run ("Rule: description"). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Audit")
	FString GetIssueText(int32 Index) const;

	/** World position of one issue of the last run. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Audit")
	FVector GetIssueLocation(int32 Index) const;

	/** Draws a marker and a label at every issue of the last run. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Audit")
	void DrawIssues(float DurationSeconds) const;

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Audit")
	void ClearDrawing() const;

	const TArray<FChronoAuditIssue>& GetIssues() const { return Issues; }

private:
	TArray<FChronoAuditIssue> Issues;
};
