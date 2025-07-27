// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraCharacter.h" // 캐릭터의 상태, 팀, 어빌리티 시스템, 체력, 카메라 등 주요 컴포넌트와 네트워크 복제, 팀 시스템, 사망 처리 등 핵심 기능을 정의합니다.

#include "AbilitySystem/LyraAbilitySystemComponent.h" // 언리얼의 GameplayAbilitySystem을 확장하여, 캐릭터의 능력치, 효과, 태그, 어빌리티 관리 기능을 제공합니다.
#include "Camera/LyraCameraComponent.h" // Lyra 전용 카메라 컴포넌트 선언부. 캐릭터에 부착되어 3인칭/1인칭 등 다양한 시점 제어를 지원합니다.
#include "Character/LyraHealthComponent.h" // Lyra 캐릭터의 체력 관리 컴포넌트 선언부. 체력, 데미지, 사망 이벤트, 체력 변화 알림 등 기능을 제공합니다.
#include "Character/LyraPawnExtensionComponent.h" // Lyra Pawn의 확장 기능을 위한 컴포넌트 선언부. 어빌리티 시스템 초기화, 입력 바인딩, 컨트롤러/플레이어 상태 변화 처리 등 확장 기능을 담당합니다.
#include "Components/CapsuleComponent.h"  
#include "Components/SkeletalMeshComponent.h" // 본 기반 애니메이션, 메시 렌더링, 충돌 등 캐릭터의 외형을 담당합니다.
#include "LyraCharacterMovementComponent.h"
#include "LyraGameplayTags.h"
#include "LyraLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "Player/LyraPlayerController.h"
#include "Player/LyraPlayerState.h"
#include "System/LyraSignificanceManager.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraCharacter)

class AActor;
class FLifetimeProperty;
class IRepChangedPropertyTracker;
class UInputComponent;

// 캡슐 및 메시 충돌 프로필 이름 정의
static FName NAME_LyraCharacterCollisionProfile_Capsule(TEXT("LyraPawnCapsule"));
static FName NAME_LyraCharacterCollisionProfile_Mesh(TEXT("LyraPawnMesh"));

// Lyra 캐릭터 생성자
ALyraCharacter::ALyraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<ULyraCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// 캐릭터의 Tick 비활성화 (성능 최적화)
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 네트워크 컬링 거리 설정
	SetNetCullDistanceSquared(900000000.0f);

	// 캡슐 컴포넌트 초기화 및 충돌 프로필 설정
	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->InitCapsuleSize(40.0f, 90.0f);
	CapsuleComp->SetCollisionProfileName(NAME_LyraCharacterCollisionProfile_Capsule);

	// 메시 컴포넌트 초기화 및 충돌 프로필 설정
	USkeletalMeshComponent* MeshComp = GetMesh();
	check(MeshComp);
	MeshComp->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));  // 메시의 Forward 방향을 X로 맞춤
	MeshComp->SetCollisionProfileName(NAME_LyraCharacterCollisionProfile_Mesh);

	// 커스텀 이동 컴포넌트 설정
	ULyraCharacterMovementComponent* LyraMoveComp = CastChecked<ULyraCharacterMovementComponent>(GetCharacterMovement());
	LyraMoveComp->GravityScale = 1.0f;
	LyraMoveComp->MaxAcceleration = 2400.0f;
	LyraMoveComp->BrakingFrictionFactor = 1.0f;
	LyraMoveComp->BrakingFriction = 6.0f;
	LyraMoveComp->GroundFriction = 8.0f;
	LyraMoveComp->BrakingDecelerationWalking = 1400.0f;
	LyraMoveComp->bUseControllerDesiredRotation = false;
	LyraMoveComp->bOrientRotationToMovement = false;
	LyraMoveComp->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	LyraMoveComp->bAllowPhysicsRotationDuringAnimRootMotion = false;
	LyraMoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	LyraMoveComp->bCanWalkOffLedgesWhenCrouching = true;
	LyraMoveComp->SetCrouchedHalfHeight(65.0f);

	// Pawn 확장 컴포넌트 생성 및 델리게이트 바인딩
	PawnExtComponent = CreateDefaultSubobject<ULyraPawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
	PawnExtComponent->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));

	// 체력 컴포넌트 생성 및 사망 이벤트 바인딩
	HealthComponent = CreateDefaultSubobject<ULyraHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
	HealthComponent->OnDeathFinished.AddDynamic(this, &ThisClass::OnDeathFinished);

	// 카메라 컴포넌트 생성 및 위치 설정
	CameraComponent = CreateDefaultSubobject<ULyraCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetRelativeLocation(FVector(-300.0f, 0.0f, 75.0f));

	// 컨트롤러 회전 설정
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// 시점 높이 설정
	BaseEyeHeight = 80.0f;
	CrouchedEyeHeight = 50.0f;
}

// 컴포넌트 초기화 전 호출
void ALyraCharacter::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

// 게임 시작 시 호출
void ALyraCharacter::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();

	// 시그니피컨스 매니저에 등록 (서버가 아닐 때만)
	const bool bRegisterWithSignificanceManager = !IsNetMode(NM_DedicatedServer);
	if (bRegisterWithSignificanceManager)
	{
		if (ULyraSignificanceManager* SignificanceManager = USignificanceManager::Get<ULyraSignificanceManager>(World))
		{
			//@TODO: SignificanceManager->RegisterObject(this, (EFortSignificanceType)SignificanceType);
		}
	}
}

// 게임 종료 시 호출
void ALyraCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	UWorld* World = GetWorld();

	// 시그니피컨스 매니저에서 등록 해제
	const bool bRegisterWithSignificanceManager = !IsNetMode(NM_DedicatedServer);
	if (bRegisterWithSignificanceManager)
	{
		if (ULyraSignificanceManager* SignificanceManager = USignificanceManager::Get<ULyraSignificanceManager>(World))
		{
			SignificanceManager->UnregisterObject(this);
		}
	}
}

// 캐릭터 리셋 (죽음 등)
void ALyraCharacter::Reset()
{
	DisableMovementAndCollision();

	K2_OnReset();

	UninitAndDestroy();
}

// 네트워크 복제 프로퍼티 등록
void ALyraCharacter::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedAcceleration, COND_SimulatedOnly);
	DOREPLIFETIME(ThisClass, MyTeamID)
}

// 복제 전 가공 처리 (가속도 압축)
void ALyraCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		// XY 가속도는 방향+크기, Z는 직접 값으로 압축
		const double MaxAccel = MovementComponent->MaxAcceleration;
		const FVector CurrentAccel = MovementComponent->GetCurrentAcceleration();
		double AccelXYRadians, AccelXYMagnitude;
		FMath::CartesianToPolar(CurrentAccel.X, CurrentAccel.Y, AccelXYMagnitude, AccelXYRadians);

		ReplicatedAcceleration.AccelXYRadians = FMath::FloorToInt((AccelXYRadians / TWO_PI) * 255.0);     // [0, 2PI] -> [0, 255]
		ReplicatedAcceleration.AccelXYMagnitude = FMath::FloorToInt((AccelXYMagnitude / MaxAccel) * 255.0);	// [0, MaxAccel] -> [0, 255]
		ReplicatedAcceleration.AccelZ = FMath::FloorToInt((CurrentAccel.Z / MaxAccel) * 127.0);   // [-MaxAccel, MaxAccel] -> [-127, 127]
	}
}

// 컨트롤러 변경 시 팀 정보 갱신
void ALyraCharacter::NotifyControllerChanged()
{
	const FGenericTeamId OldTeamId = GetGenericTeamId();

	Super::NotifyControllerChanged();

	// 컨트롤러에서 팀 ID 갱신
	if (HasAuthority() && (Controller != nullptr))
	{
		if (ILyraTeamAgentInterface* ControllerWithTeam = Cast<ILyraTeamAgentInterface>(Controller))
		{
			MyTeamID = ControllerWithTeam->GetGenericTeamId();
			ConditionalBroadcastTeamChanged(this, OldTeamId, MyTeamID);
		}
	}
}

// Lyra 전용 PlayerController 반환
ALyraPlayerController* ALyraCharacter::GetLyraPlayerController() const
{
	return CastChecked<ALyraPlayerController>(Controller, ECastCheckedType::NullAllowed);
}

// Lyra 전용 PlayerState 반환
ALyraPlayerState* ALyraCharacter::GetLyraPlayerState() const
{
	return CastChecked<ALyraPlayerState>(GetPlayerState(), ECastCheckedType::NullAllowed);
}

// Lyra 전용 AbilitySystemComponent 반환
ULyraAbilitySystemComponent* ALyraCharacter::GetLyraAbilitySystemComponent() const
{
	return Cast<ULyraAbilitySystemComponent>(GetAbilitySystemComponent());
}

// AbilitySystemComponent 반환 (Pawn 확장 컴포넌트에서)
UAbilitySystemComponent* ALyraCharacter::GetAbilitySystemComponent() const
{
	if (PawnExtComponent == nullptr)
	{
		return nullptr;
	}

	return PawnExtComponent->GetLyraAbilitySystemComponent();
}

// 어빌리티 시스템 초기화 시 호출
void ALyraCharacter::OnAbilitySystemInitialized()
{
	ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent();
	check(LyraASC);

	HealthComponent->InitializeWithAbilitySystem(LyraASC);

	InitializeGameplayTags();
}

// 어빌리티 시스템 해제 시 호출
void ALyraCharacter::OnAbilitySystemUninitialized()
{
	HealthComponent->UninitializeFromAbilitySystem();
}

// 캐릭터가 컨트롤러에 소유될 때 호출
void ALyraCharacter::PossessedBy(AController* NewController)
{
	const FGenericTeamId OldTeamID = MyTeamID;

	Super::PossessedBy(NewController);

	PawnExtComponent->HandleControllerChanged();

	// 팀 정보 갱신 및 델리게이트 등록
	if (ILyraTeamAgentInterface* ControllerAsTeamProvider = Cast<ILyraTeamAgentInterface>(NewController))
	{
		MyTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnControllerChangedTeam);
	}
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

// 캐릭터가 컨트롤러에서 해제될 때 호출
void ALyraCharacter::UnPossessed()
{
	AController* const OldController = Controller;

	// 이전 컨트롤러의 팀 변경 델리게이트 해제
	const FGenericTeamId OldTeamID = MyTeamID;
	if (ILyraTeamAgentInterface* ControllerAsTeamProvider = Cast<ILyraTeamAgentInterface>(OldController))
	{
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().RemoveAll(this);
	}

	Super::UnPossessed();

	PawnExtComponent->HandleControllerChanged();

	// 새로운 팀 ID 결정
	MyTeamID = DetermineNewTeamAfterPossessionEnds(OldTeamID);
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

// 컨트롤러 복제 시 호출
void ALyraCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	PawnExtComponent->HandleControllerChanged();
}

// PlayerState 복제 시 호출
void ALyraCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	PawnExtComponent->HandlePlayerStateReplicated();
}

// 입력 바인딩
void ALyraCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PawnExtComponent->SetupPlayerInputComponent();
}

// 게임플레이 태그 초기화
void ALyraCharacter::InitializeGameplayTags()
{
	// 이전 Pawn에서 남아있을 수 있는 태그 제거
	if (ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		for (const TPair<uint8, FGameplayTag>& TagMapping : LyraGameplayTags::MovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				LyraASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		for (const TPair<uint8, FGameplayTag>& TagMapping : LyraGameplayTags::CustomMovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				LyraASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		ULyraCharacterMovementComponent* LyraMoveComp = CastChecked<ULyraCharacterMovementComponent>(GetCharacterMovement());
		SetMovementModeTag(LyraMoveComp->MovementMode, LyraMoveComp->CustomMovementMode, true);
	}
}

// 소유한 게임플레이 태그 반환
void ALyraCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	if (const ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		LyraASC->GetOwnedGameplayTags(TagContainer);
	}
}

// 특정 태그 보유 여부 확인
bool ALyraCharacter::HasMatchingGameplayTag(FGameplayTag TagToCheck) const
{
	if (const ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		return LyraASC->HasMatchingGameplayTag(TagToCheck);
	}

	return false;
}

// 모든 태그 보유 여부 확인
bool ALyraCharacter::HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		return LyraASC->HasAllMatchingGameplayTags(TagContainer);
	}

	return false;
}

// 하나라도 태그 보유 여부 확인
bool ALyraCharacter::HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		return LyraASC->HasAnyMatchingGameplayTags(TagContainer);
	}

	return false;
}

// 월드 밖으로 떨어졌을 때 호출
void ALyraCharacter::FellOutOfWorld(const class UDamageType& dmgType)
{
	HealthComponent->DamageSelfDestruct(/*bFellOutOfWorld=*/ true);
}

// 사망 시작 시 호출
void ALyraCharacter::OnDeathStarted(AActor*)
{
	DisableMovementAndCollision();
}

// 사망 종료 시 호출
void ALyraCharacter::OnDeathFinished(AActor*)
{
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::DestroyDueToDeath);
}

// 이동 및 충돌 비활성화
void ALyraCharacter::DisableMovementAndCollision()
{
	if (Controller)
	{
		Controller->SetIgnoreMoveInput(true);
	}

	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapsuleComp->SetCollisionResponseToAllChannels(ECR_Ignore);

	ULyraCharacterMovementComponent* LyraMoveComp = CastChecked<ULyraCharacterMovementComponent>(GetCharacterMovement());
	LyraMoveComp->StopMovementImmediately();
	LyraMoveComp->DisableMovement();
}

// 사망으로 인한 파괴 처리
void ALyraCharacter::DestroyDueToDeath()
{
	K2_OnDeathFinished();

	UninitAndDestroy();
}

// 언리얼 및 ASC 해제 후 파괴
void ALyraCharacter::UninitAndDestroy()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		DetachFromControllerPendingDestroy();
		SetLifeSpan(0.1f);
	}

	// ASC가 아직 이 Pawn을 Avatar로 가지고 있다면 해제
	if (ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		if (LyraASC->GetAvatarActor() == this)
		{
			PawnExtComponent->UninitializeAbilitySystem();
		}
	}

	SetActorHiddenInGame(true);
}

// 이동 모드 변경 시 태그 갱신
void ALyraCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	ULyraCharacterMovementComponent* LyraMoveComp = CastChecked<ULyraCharacterMovementComponent>(GetCharacterMovement());

	SetMovementModeTag(PrevMovementMode, PreviousCustomMode, false);
	SetMovementModeTag(LyraMoveComp->MovementMode, LyraMoveComp->CustomMovementMode, true);
}

// 이동 모드에 따른 태그 설정
void ALyraCharacter::SetMovementModeTag(EMovementMode MovementMode, uint8 CustomMovementMode, bool bTagEnabled)
{
	if (ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		const FGameplayTag* MovementModeTag = nullptr;
		if (MovementMode == MOVE_Custom)
		{
			MovementModeTag = LyraGameplayTags::CustomMovementModeTagMap.Find(CustomMovementMode);
		}
		else
		{
			MovementModeTag = LyraGameplayTags::MovementModeTagMap.Find(MovementMode);
		}

		if (MovementModeTag && MovementModeTag->IsValid())
		{
			LyraASC->SetLooseGameplayTagCount(*MovementModeTag, (bTagEnabled ? 1 : 0));
		}
	}
}

// 앉기/서기 토글
void ALyraCharacter::ToggleCrouch()
{
	const ULyraCharacterMovementComponent* LyraMoveComp = CastChecked<ULyraCharacterMovementComponent>(GetCharacterMovement());

	if (bIsCrouched || LyraMoveComp->bWantsToCrouch)
	{
		UnCrouch();
	}
	else if (LyraMoveComp->IsMovingOnGround())
	{
		Crouch();
	}
}

// 앉기 시작 시 태그 추가
void ALyraCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		LyraASC->SetLooseGameplayTagCount(LyraGameplayTags::Status_Crouching, 1);
	}

	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

// 앉기 종료 시 태그 제거
void ALyraCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (ULyraAbilitySystemComponent* LyraASC = GetLyraAbilitySystemComponent())
	{
		LyraASC->SetLooseGameplayTagCount(LyraGameplayTags::Status_Crouching, 0);
	}

	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

// 점프 가능 여부 (앉기 체크 제외)
bool ALyraCharacter::CanJumpInternal_Implementation() const
{
	// ACharacter 기본 구현과 동일하나, 앉기 체크는 제외
	return JumpIsAllowedInternal();
}

// 가속도 복제 시 압축 해제
void ALyraCharacter::OnRep_ReplicatedAcceleration()
{
	if (ULyraCharacterMovementComponent* LyraMovementComponent = Cast<ULyraCharacterMovementComponent>(GetCharacterMovement()))
	{
		// 압축된 가속도 복원
		const double MaxAccel = LyraMovementComponent->MaxAcceleration;
		const double AccelXYMagnitude = double(ReplicatedAcceleration.AccelXYMagnitude) * MaxAccel / 255.0; // [0, 255] -> [0, MaxAccel]
		const double AccelXYRadians = double(ReplicatedAcceleration.AccelXYRadians) * TWO_PI / 255.0;     // [0, 255] -> [0, 2PI]

		FVector UnpackedAcceleration(FVector::ZeroVector);
		FMath::PolarToCartesian(AccelXYMagnitude, AccelXYRadians, UnpackedAcceleration.X, UnpackedAcceleration.Y);
		UnpackedAcceleration.Z = double(ReplicatedAcceleration.AccelZ) * MaxAccel / 127.0; // [-127, 127] -> [-MaxAccel, MaxAccel]

		LyraMovementComponent->SetReplicatedAcceleration(UnpackedAcceleration);
	}
}

// 팀 ID 직접 설정 (컨트롤러가 없을 때만)
void ALyraCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (GetController() == nullptr)
	{
		if (HasAuthority())
		{
			const FGenericTeamId OldTeamID = MyTeamID;
			MyTeamID = NewTeamID;
			ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
		}
		else
		{
			UE_LOG(LogLyraTeams, Error, TEXT("You can't set the team ID on a character (%s) except on the authority"), *GetPathNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogLyraTeams, Error, TEXT("You can't set the team ID on a possessed character (%s); it's driven by the associated controller"), *GetPathNameSafe(this));
	}
}

// 팀 ID 반환
FGenericTeamId ALyraCharacter::GetGenericTeamId() const
{
	return MyTeamID;
}

// 팀 변경 델리게이트 반환
FOnLyraTeamIndexChangedDelegate* ALyraCharacter::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

// 컨트롤러의 팀 변경 시 호출
void ALyraCharacter::OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	const FGenericTeamId MyOldTeamID = MyTeamID;
	MyTeamID = IntegerToGenericTeamId(NewTeam);
	ConditionalBroadcastTeamChanged(this, MyOldTeamID, MyTeamID);
}

// 팀 ID 복제 시 호출
void ALyraCharacter::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

// 공유 복제 데이터 갱신 (네트워크 최적화)
bool ALyraCharacter::UpdateSharedReplication()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		FSharedRepMovement SharedMovement;
		if (SharedMovement.FillForCharacter(this))
		{
			// 데이터가 변경된 경우에만 FastSharedReplication 호출
			if (!SharedMovement.Equals(LastSharedReplication, this))
			{
				LastSharedReplication = SharedMovement;
				ReplicatedMovementMode = SharedMovement.RepMovementMode;

				FastSharedReplication(SharedMovement);
			}
			return true;
		}
	}

	// 현재는 fastrep 불가
	return false;
}

// FastSharedReplication 구현 (클라이언트 동기화)
void ALyraCharacter::FastSharedReplication_Implementation(const FSharedRepMovement& SharedRepMovement)
{
	if (GetWorld()->IsPlayingReplay())
	{
		return;
	}

	// 타임스탬프를 통해 이전 데이터 거부
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		// 타임스탬프
		ReplicatedServerLastTransformUpdateTimeStamp = SharedRepMovement.RepTimeStamp;

		// 이동 모드
		if (ReplicatedMovementMode != SharedRepMovement.RepMovementMode)
		{
			ReplicatedMovementMode = SharedRepMovement.RepMovementMode;
			GetCharacterMovement()->bNetworkMovementModeChanged = true;
			GetCharacterMovement()->bNetworkUpdateReceived = true;
		}

		// 위치, 회전, 속도 등
		FRepMovement& MutableRepMovement = GetReplicatedMovement_Mutable();
		MutableRepMovement = SharedRepMovement.RepMovement;

		// LastRepMovement도 갱신
		OnRep_ReplicatedMovement();

		// 점프 force
		bProxyIsJumpForceApplied = SharedRepMovement.bProxyIsJumpForceApplied;

		// 앉기 상태
		if (bIsCrouched != SharedRepMovement.bIsCrouched)
		{
			bIsCrouched = SharedRepMovement.bIsCrouched;
			OnRep_IsCrouched();
		}
	}
}

// FSharedRepMovement 생성자 (위치 정밀도 설정)
FSharedRepMovement::FSharedRepMovement()
{
	RepMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
}

// 캐릭터의 현재 상태로 FSharedRepMovement 채우기
bool FSharedRepMovement::FillForCharacter(ACharacter* Character)
{
	if (USceneComponent* PawnRootComponent = Character->GetRootComponent())
	{
		UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();

		RepMovement.Location = FRepMovement::RebaseOntoZeroOrigin(PawnRootComponent->GetComponentLocation(), Character);
		RepMovement.Rotation = PawnRootComponent->GetComponentRotation();
		RepMovement.LinearVelocity = CharacterMovement->Velocity;
		RepMovementMode = CharacterMovement->PackNetworkMovementMode();
		bProxyIsJumpForceApplied = Character->bProxyIsJumpForceApplied || (Character->JumpForceTimeRemaining > 0.0f);
		bIsCrouched = Character->bIsCrouched;

		// 타임스탬프 (네트워크 스무딩 모드에 따라)
		if ((CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Linear) || CharacterMovement->bNetworkAlwaysReplicateTransformUpdateTimestamp)
		{
			RepTimeStamp = CharacterMovement->GetServerLastTransformUpdateTimeStamp();
		}
		else
		{
			RepTimeStamp = 0.f;
		}

		return true;
	}
	return false;
}

// FSharedRepMovement 비교 (동일 여부)
bool FSharedRepMovement::Equals(const FSharedRepMovement& Other, ACharacter* Character) const
{
	if (RepMovement.Location != Other.RepMovement.Location)
	{
		return false;
	}

	if (RepMovement.Rotation != Other.RepMovement.Rotation)
	{
		return false;
	}

	if (RepMovement.LinearVelocity != Other.RepMovement.LinearVelocity)
	{
		return false;
	}

	if (RepMovementMode != Other.RepMovementMode)
	{
		return false;
	}

	if (bProxyIsJumpForceApplied != Other.bProxyIsJumpForceApplied)
	{
		return false;
	}

	if (bIsCrouched != Other.bIsCrouched)
	{
		return false;
	}

	return true;
}

// FSharedRepMovement 네트워크 직렬화
bool FSharedRepMovement::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;
	RepMovement.NetSerialize(Ar, Map, bOutSuccess);
	Ar << RepMovementMode;
	Ar << bProxyIsJumpForceApplied;
	Ar << bIsCrouched;

	// 타임스탬프가 0이 아니면 직렬화
	uint8 bHasTimeStamp = (RepTimeStamp != 0.f);
	Ar.SerializeBits(&bHasTimeStamp, 1);
	if (bHasTimeStamp)
	{
		Ar << RepTimeStamp;
	}
	else
	{
		RepTimeStamp = 0.f;
	}

	return true;
}