#pragma once
/* Private kernel implementation. Caller serializes Initialize/Challenge/Accept;
   Allowed is safe at <= DISPATCH_LEVEL while the containing file context lives.
   No reusable commercial secret is present in the driver or public SDK. */
#include <ntddk.h>
#include <bcrypt.h>
#include "DriverAuthorizationProtocol.h"
#include "DriverAuthorizationPublicKey.h"

typedef __declspec(align(8)) struct TITAN_AUTHORIZATION_STATE {
    volatile LONG64 deadline;
    ULONGLONG challenge_created;
    ULONG product;
    BOOLEAN challenge_valid;
    UCHAR nonce[32];
} TITAN_AUTHORIZATION_STATE;

static __inline VOID TitanAuthorizationInitialize(
    TITAN_AUTHORIZATION_STATE* state, ULONG product) {
    RtlZeroMemory(state, sizeof(*state));
    state->product = product;
}

static __inline BOOLEAN TitanAuthorizationAllowed(
    TITAN_AUTHORIZATION_STATE* state) {
    ULONGLONG deadline = (ULONGLONG)InterlockedCompareExchange64(
        &state->deadline, 0, 0);
    return deadline != 0 && KeQueryInterruptTime() < deadline;
}

_IRQL_requires_(PASSIVE_LEVEL)
static __inline NTSTATUS TitanAuthorizationChallenge(
    TITAN_AUTHORIZATION_STATE* state,
    TITAN_AUTHORIZATION_CHALLENGE* challenge) {
    NTSTATUS status;
    state->challenge_valid = FALSE;
    RtlSecureZeroMemory(state->nonce, sizeof(state->nonce));
    status = BCryptGenRandom(NULL, state->nonce, sizeof(state->nonce),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!NT_SUCCESS(status)) return status;
    state->challenge_created = KeQueryInterruptTime();
    state->challenge_valid = TRUE;
    RtlZeroMemory(challenge, sizeof(*challenge));
    challenge->magic = TITAN_AUTHORIZATION_MAGIC;
    challenge->version = TITAN_AUTHORIZATION_VERSION;
    challenge->product = state->product;
    RtlCopyMemory(challenge->nonce, state->nonce, sizeof(state->nonce));
    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
static __inline NTSTATUS TitanAuthorizationAccept(
    TITAN_AUTHORIZATION_STATE* state,
    const TITAN_AUTHORIZATION_TOKEN* token) {
    BCRYPT_ALG_HANDLE hash_algorithm = NULL, signature_algorithm = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR digest[32];
    NTSTATUS status;
    ULONGLONG now = KeQueryInterruptTime();
    if (!state->challenge_valid || now < state->challenge_created ||
        now - state->challenge_created > 120ULL * 10000000ULL ||
        token->permission.magic != TITAN_AUTHORIZATION_MAGIC ||
        token->permission.version != TITAN_AUTHORIZATION_VERSION ||
        token->permission.product != state->product ||
        token->permission.duration_seconds == 0 ||
        token->permission.duration_seconds > TITAN_AUTHORIZATION_MAX_SECONDS ||
        RtlCompareMemory(token->permission.nonce, state->nonce, 32) != 32)
        return STATUS_ACCESS_DENIED;
    /* Consume the challenge even on a bad signature. Obtain a new challenge
       to retry; an accepted permission cannot be replayed to extend its lease. */
    state->challenge_valid = FALSE;
    RtlSecureZeroMemory(state->nonce, sizeof(state->nonce));
    status = BCryptOpenAlgorithmProvider(&hash_algorithm, BCRYPT_SHA256_ALGORITHM,
        NULL, 0);
    if (!NT_SUCCESS(status)) goto Cleanup;
    status = BCryptHash(hash_algorithm, NULL, 0,
        (PUCHAR)&token->permission, sizeof(token->permission), digest,
        sizeof(digest));
    if (!NT_SUCCESS(status)) goto Cleanup;
    status = BCryptOpenAlgorithmProvider(&signature_algorithm,
        BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status)) goto Cleanup;
    status = BCryptImportKeyPair(signature_algorithm, NULL, BCRYPT_ECCPUBLIC_BLOB,
        &key, (PUCHAR)TitanAuthorizationPublicKey,
        sizeof(TitanAuthorizationPublicKey), 0);
    if (!NT_SUCCESS(status)) goto Cleanup;
    status = BCryptVerifySignature(key, NULL, digest, sizeof(digest),
        (PUCHAR)token->signature, sizeof(token->signature), 0);
    if (!NT_SUCCESS(status)) goto Cleanup;
    now = KeQueryInterruptTime();
    if (now < state->challenge_created ||
        now - state->challenge_created > 120ULL * 10000000ULL) {
        status = STATUS_ACCESS_DENIED;
        goto Cleanup;
    }
    InterlockedExchange64(&state->deadline,
        (LONG64)(now + (ULONGLONG)token->permission.duration_seconds * 10000000ULL));
Cleanup:
    if (key) BCryptDestroyKey(key);
    if (signature_algorithm) BCryptCloseAlgorithmProvider(signature_algorithm, 0);
    if (hash_algorithm) BCryptCloseAlgorithmProvider(hash_algorithm, 0);
    RtlSecureZeroMemory(digest, sizeof(digest));
    return status;
}
