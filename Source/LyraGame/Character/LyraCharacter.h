// Lyra 프로젝트의 기본 캐릭터 클래스 헤더
#pragma once

// 언리얼 및 Lyra 관련 인터페이스/기반 클래스 포함
#include "AbilitySystemInterface.h"         // 어빌리티 시스템 연동
#include "GameplayCueInterface.h"           // 게임플레이 큐(이펙트 등) 연동
#include "GameplayTagAssetInterface.h"      // 게임플레이 태그 연동
#include "ModularCharacter.h"               // 모듈형 캐릭터(확장성)
#include "Teams/LyraTeamAgentInterface.h"   // 팀 시스템 연동

#include "LyraCharacter.generated.h"

// 전방 선언: 다양한 언리얼/프로젝트 클래스 및 구조체
class AActor;                      // 기본 액터
class AController;                 // 컨트롤러(플레이어/AI)
class ALyraPlayerController;        // Lyra 전용 플레이어 컨트롤러
class ALyraPlayerState;             // Lyra 전용 플레이어 상태
class FLifetimeProperty;            // 네트워크 동기화용 프로퍼티
class IRepChangedPropertyTracker;   // 동기화 변경 추적기
class UAbilitySystemComponent;      // 언리얼 어빌리티 시스템
class UInputComponent;              // 입력 컴포넌트
class ULyraAbilitySystemComponent;  // Lyra 전용 어빌리티 시스템
class ULyraCameraComponent;         // Lyra 전용 카메라
class ULyraHealthComponent;         // Lyra 전용 체력
class ULyraPawnExtensionComponent;  // Lyra 전용 폰 확장
class UObject;                      // 언리얼 오브젝트
struct FFrame;                      // 프레임
struct FGameplayTag;                // 게임플레이 태그
struct FGameplayTagContainer;       // 게임플레이 태그 컨테이너

// --- 네트워크용 가속도 구조체 ---
/**
 * FLyraReplicatedAcceleration: 가속도 정보를 네트워크에 최적화된 형태로 압축 저장
 */
USTRUCT()
struct FLyraReplicatedAcceleration
{
    GENERATED_BODY()

    UPROPERTY()
    uint8 AccelXYRadians = 0;   // XY 평면 가속 방향(라디안, 0~2pi)

    UPROPERTY()
    uint8 AccelXYMagnitude = 0; // XY 평면 가속 크기(0~MaxAcceleration)

    UPROPERTY()
    int8 AccelZ = 0;            // Z축 가속도(-MaxAcceleration~MaxAcceleration)
};

// --- 네트워크용 이동 정보 구조체 ---
/**
 * FSharedRepMovement: 캐릭터의 이동 상태를 네트워크로 빠르게 동기화할 때 사용
 */
USTRUCT()
struct FSharedRepMovement
{
    GENERATED_BODY()

    FSharedRepMovement();

    bool FillForCharacter(ACharacter* Character); // 캐릭터 상태로부터 값 채우기
    bool Equals(const FSharedRepMovement& Other, ACharacter* Character) const; // 비교
    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess); // 네트워크 직렬화

    UPROPERTY(Transient)
    FRepMovement RepMovement;    // 기본 이동 정보

    UPROPERTY(Transient)
    float RepTimeStamp = 0.0f;  // 동기화 시각

    UPROPERTY(Transient)
    uint8 RepMovementMode = 0;  // 이동 모드(걷기, 점프 등)

    UPROPERTY(Transient)
    bool bProxyIsJumpForceApplied = false; // 점프 힘 적용 여부

    UPROPERTY(Transient)
    bool bIsCrouched = false;   // 앉기 상태 여부
};

// 네트워크 직렬화 지원을 위한 Traits
template<>
struct TStructOpsTypeTraits<FSharedRepMovement> : public TStructOpsTypeTraitsBase2<FSharedRepMovement>
{
    enum
    {
        WithNetSerializer = true,
        WithNetSharedSerialization = true,
    };
};

// --- Lyra 캐릭터 클래스 ---
/**
 * ALyraCharacter
 *
 * Lyra 프로젝트의 기본 캐릭터 폰 클래스.
 * - 다양한 컴포넌트(카메라, 체력, 어빌리티 등)와 연동
 * - 팀, 태그, 네트워크 동기화, 사망 처리 등 지원
 * - 새로운 동작은 가급적 컴포넌트로 확장
 */
UCLASS(Config = Game, Meta = (ShortTooltip = "The base character pawn class used by this project."))
class LYRAGAME_API ALyraCharacter
    : public AModularCharacter
    , public IAbilitySystemInterface
    , public IGameplayCueInterface
    , public IGameplayTagAssetInterface
    , public ILyraTeamAgentInterface
{
    GENERATED_BODY()

public:
    // --- 생성자 ---
    ALyraCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    // --- 플레이어/상태/어빌리티 시스템 접근 ---
    UFUNCTION(BlueprintCallable, Category = "Lyra|Character")
    ALyraPlayerController* GetLyraPlayerController() const;

    UFUNCTION(BlueprintCallable, Category = "Lyra|Character")
    ALyraPlayerState* GetLyraPlayerState() const;

    UFUNCTION(BlueprintCallable, Category = "Lyra|Character")
    ULyraAbilitySystemComponent* GetLyraAbilitySystemComponent() const;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    // --- 게임플레이 태그 관련 ---
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
    virtual bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const override;
    virtual bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
    virtual bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;

    // --- 앉기 토글 ---
    void ToggleCrouch();

    // --- AActor 인터페이스 오버라이드 ---
    virtual void PreInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Reset() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

    // --- APawn 인터페이스 오버라이드 ---
    virtual void NotifyControllerChanged() override;

    // --- 팀 시스템 인터페이스 오버라이드 ---
    virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
    virtual FGenericTeamId GetGenericTeamId() const override;
    virtual FOnLyraTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;

    // --- 네트워크 이동 동기화(RPC) ---
    UFUNCTION(NetMulticast, unreliable)
    void FastSharedReplication(const FSharedRepMovement& SharedRepMovement);

    FSharedRepMovement LastSharedReplication; // 마지막으로 전송한 이동 정보
    virtual bool UpdateSharedReplication();

protected:
    // --- 어빌리티 시스템 초기화/해제 ---
    virtual void OnAbilitySystemInitialized();
    virtual void OnAbilitySystemUninitialized();

    // --- 소유/해제, 컨트롤러/플레이어 상태 변경 ---
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void OnRep_Controller() override;
    virtual void OnRep_PlayerState() override;

    // --- 입력 바인딩 ---
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // --- 태그 초기화 ---
    void InitializeGameplayTags();

    // --- 월드 밖으로 떨어졌을 때 처리 ---
    virtual void FellOutOfWorld(const class UDamageType& dmgType) override;

    // --- 사망 처리 ---
    UFUNCTION()
    virtual void OnDeathStarted(AActor* OwningActor); // 사망 시작(충돌/이동 비활성화 등)

    UFUNCTION()
    virtual void OnDeathFinished(AActor* OwningActor); // 사망 종료(컨트롤러 분리, 파괴 등)

    void DisableMovementAndCollision(); // 이동/충돌 비활성화
    void DestroyDueToDeath();           // 사망으로 인한 파괴
    void UninitAndDestroy();            // 언리얼 오브젝트 해제 및 파괴

    UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnDeathFinished"))
    void K2_OnDeathFinished(); // 블루프린트용 사망 종료 이벤트

    // --- 이동 모드 변경 ---
    virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
    void SetMovementModeTag(EMovementMode MovementMode, uint8 CustomMovementMode, bool bTagEnabled);

    // --- 앉기/일어서기 ---
    virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

    // --- 점프 가능 여부 ---
    virtual bool CanJumpInternal_Implementation() const;

private:
    // --- Lyra 전용 컴포넌트 ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lyra|Character", Meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULyraPawnExtensionComponent> PawnExtComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lyra|Character", Meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULyraHealthComponent> HealthComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lyra|Character", Meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULyraCameraComponent> CameraComponent;

    // --- 네트워크 동기화용 변수 ---
    UPROPERTY(Transient, ReplicatedUsing = OnRep_ReplicatedAcceleration)
    FLyraReplicatedAcceleration ReplicatedAcceleration;

    UPROPERTY(ReplicatedUsing = OnRep_MyTeamID)
    FGenericTeamId MyTeamID;

    UPROPERTY()
    FOnLyraTeamIndexChangedDelegate OnTeamChangedDelegate;

protected:
    // --- 소유 해제 시 팀 결정 ---
    virtual FGenericTeamId DetermineNewTeamAfterPossessionEnds(FGenericTeamId OldTeamID) const
    {
        // 기본: 팀 해제(NoTeam)로 설정
        return FGenericTeamId::NoTeam;
    }

private:
    // --- 내부 콜백 함수 ---
    UFUNCTION()
    void OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

    UFUNCTION()
    void OnRep_ReplicatedAcceleration();

    UFUNCTION()
    void OnRep_MyTeamID(FGenericTeamId OldTeamID);
};