#pragma once

// Launcher authority -- the handshake that makes the 3DG Launcher the ONLY way to open the editor.
//
// The launcher mints a signed, short-lived launch token bound to the project it is opening and passes
// it to 3DGEditor on the command line. The editor validates the token at startup; if it is missing,
// stale, tampered, or bound to a different project, the editor refuses to run standalone and instead
// re-launches the launcher (so the editor "can only be opened again through the created project or the
// launcher"). This is workflow enforcement, not cryptographic security -- a shared secret compiled
// into both binaries plus a freshness window is enough to stop direct/accidental launches of the
// editor executable. Header-only, dependency-free, unit-testable.

#include <cstdint>
#include <string>

namespace engine {

// Shared secret baked into the launcher and the editor. Changing it invalidates old tokens.
inline constexpr std::uint64_t kLaunchSecret = 0x33'44'47'4C'4E'43'48'52ull; // "3DGLNCHR"

// Command-line flag the launcher uses to pass the token: 3DGEditor --launch-token=<token> "<project>"
inline constexpr const char* kLaunchTokenFlag = "--launch-token=";

// FNV-1a 64 over a byte range (stable across compilers/platforms).
inline std::uint64_t Fnv1a(const void* data, std::size_t n, std::uint64_t seed = 1469598103934665603ull) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
inline std::uint64_t HashString(const std::string& s) { return Fnv1a(s.data(), s.size()); }

namespace detail {
inline std::string HexU64(std::uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) { s[i] = d[v & 0xF]; v >>= 4; }
    return s;
}
inline bool ParseHexU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 16) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint64_t>(c - 'A' + 10);
        else return false;
    }
    out = v; return true;
}
// The signature binds nonce + issue time + project to the secret.
inline std::uint64_t Sign(std::uint64_t nonce, std::uint64_t issuedMs, std::uint64_t projectHash) {
    std::uint64_t h = kLaunchSecret;
    const std::uint64_t parts[3] = { nonce, issuedMs, projectHash };
    h = Fnv1a(parts, sizeof(parts), h);
    return h;
}
} // namespace detail

// Launcher side: mint a token for `projectFile`. `nowMs` is the current unix time in ms; `nonce` is a
// random 64-bit value (e.g. from the RNG or a counter). Token = nonce.issued.projectHash.sig (hex).
inline std::string MintLaunchToken(const std::string& projectFile, std::uint64_t nowMs, std::uint64_t nonce) {
    const std::uint64_t projectHash = HashString(projectFile);
    const std::uint64_t sig = detail::Sign(nonce, nowMs, projectHash);
    return detail::HexU64(nonce) + "." + detail::HexU64(nowMs) + "." +
           detail::HexU64(projectHash) + "." + detail::HexU64(sig);
}

// Editor side: validate a token. Requires a correct signature, an issue time within [now-maxAge, now+skew],
// and (when projectFile is non-empty) that the token was minted for exactly that project. Returns true
// only when the editor was legitimately started by the launcher for this project.
inline bool ValidateLaunchToken(const std::string& token, std::uint64_t nowMs, std::uint64_t maxAgeMs,
                                const std::string& projectFile, std::string* error = nullptr) {
    const auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (token.empty()) return fail("no launch token (the editor must be started from the 3DG Launcher)");

    // split into 4 hex fields
    std::string parts[4]; int n = 0; std::string cur;
    for (char c : token) {
        if (c == '.') { if (n < 4) parts[n] = cur; ++n; cur.clear(); }
        else cur += c;
    }
    if (n != 3) return fail("malformed launch token");
    parts[3] = cur;

    std::uint64_t nonce, issued, projectHash, sig;
    if (!detail::ParseHexU64(parts[0], nonce) || !detail::ParseHexU64(parts[1], issued) ||
        !detail::ParseHexU64(parts[2], projectHash) || !detail::ParseHexU64(parts[3], sig))
        return fail("malformed launch token fields");

    if (detail::Sign(nonce, issued, projectHash) != sig) return fail("invalid launch token signature");

    // Freshness: reject stale (replayed) tokens; allow small clock skew forward.
    constexpr std::uint64_t kSkewMs = 5000;
    if (issued > nowMs + kSkewMs) return fail("launch token is from the future (clock skew)");
    if (nowMs > issued && nowMs - issued > maxAgeMs) return fail("launch token has expired -- reopen from the launcher");

    // Project binding: the token must be for the project the editor is opening.
    if (!projectFile.empty() && HashString(projectFile) != projectHash)
        return fail("launch token does not match the project being opened");

    return true;
}

} // namespace engine
