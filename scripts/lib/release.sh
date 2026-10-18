#!/usr/bin/env bash
# scripts/lib/release.sh - Packaging and production release module for Navi
set -u

do_release() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Build Navi binaries from source"
        log_dry "Update prod-release/ distribution payload"
        log_dry "Generate prod-release/update-manifest.yaml with SHA-256 hashes"
        log_dry "Package navi.zip release archive"
        return 0
    fi

    log_info "Starting release build and packaging..."
    cd "$REPO_ROOT"

    # 1. Build
    if ! make; then
        if [ -f "$REPO_ROOT/docker/build.sh" ]; then
            log_warn "Host build failed, falling back to docker build..."
            "$REPO_ROOT/docker/build.sh"
        else
            log_error "Build failed."
            return 1
        fi
    fi

    # 2. Update prod-release/navi
    local prod_dir="$REPO_ROOT/prod-release"
    local prod_navi="$prod_dir/navi"
    mkdir -p "$prod_navi/bin" "$prod_navi/res"

    cp -f "$REPO_ROOT/bin/libnavi.so" "$prod_navi/bin/"
    cp -f "$REPO_ROOT/bin/library-inject.so" "$prod_navi/bin/"

    local prev_hashes=0
    if [ -f "$prod_navi/res/updates.yaml" ]; then
        prev_hashes=$(grep -cE '^\s*-\s+[0-9a-f]{64}' "$prod_navi/res/updates.yaml" || true)
    fi
    local new_hashes
    new_hashes=$(grep -cE '^\s*-\s+[0-9a-f]{64}' "$REPO_ROOT/res/updates.yaml")
    if [ "$new_hashes" -lt "$prev_hashes" ]; then
        log_error "updates.yaml hash count decreased ($new_hashes < $prev_hashes)! Aborting."
        return 1
    fi
    log_info "Verified updates.yaml: $new_hashes hashes (previous: $prev_hashes)"

    cp -f "$REPO_ROOT/res/updates.yaml" "$prod_navi/res/"
    cp -f "$REPO_ROOT/res/config.yaml" "$prod_navi/res/"
    [ -f "$REPO_ROOT/res/config.diff.yaml" ] && cp -f "$REPO_ROOT/res/config.diff.yaml" "$prod_navi/res/"
    [ -f "$REPO_ROOT/res/cache.diff.yaml" ] && cp -f "$REPO_ROOT/res/cache.diff.yaml" "$prod_navi/res/"
    cp -f "$REPO_ROOT/res/version" "$prod_navi/res/"
    cp -f "$REPO_ROOT/res/update-all.sh" "$prod_navi/res/"
    if [ -d "$REPO_ROOT/res/inject-scripts" ]; then
        rm -rf "$prod_navi/res/inject-scripts"
        cp -r "$REPO_ROOT/res/inject-scripts" "$prod_navi/res/"
    fi
    if [ -d "$REPO_ROOT/tools" ]; then
        rm -rf "$prod_navi/tools"
        cp -r "$REPO_ROOT/tools" "$prod_navi/"
    fi
    if [ -d "$REPO_ROOT/docs" ]; then
        rm -rf "$prod_navi/docs"
        cp -r "$REPO_ROOT/docs" "$prod_navi/"
    fi

    # 3. Copy unified scripts
    cp -f "$REPO_ROOT/install.sh" "$prod_dir/"
    chmod +x "$prod_dir/install.sh"
    mkdir -p "$prod_dir/scripts/lib"
    cp -f "$REPO_ROOT/scripts/lib/"*.sh "$prod_dir/scripts/lib/"

    # 4. Generate update-manifest.yaml
    log_info "Generating update-manifest.yaml..."
    cd "$prod_dir"
    local ver_str
    ver_str=$(grep '^Version:' navi/res/version 2>/dev/null | cut -d' ' -f2 | xargs || echo '1.0.0')
    printf "version: \"%s\"\nfiles:\n" "$ver_str" > update-manifest.yaml
    find navi install.sh $( [ -d scripts ] && echo scripts ) -type f | sort | while read -r f; do
        local hash
        hash=$(sha256sum "$f" | awk '{print $1}')
        printf "  \"%s\": \"%s\"\n" "$f" "$hash" >> update-manifest.yaml
    done

    # 5. Package navi.zip
    log_info "Packaging navi.zip..."
    rm -f "$REPO_ROOT/navi.zip"
    zip -q -r "$REPO_ROOT/navi.zip" install.sh update-manifest.yaml navi $( [ -d scripts ] && echo scripts )
    cd "$REPO_ROOT"

    log_info "Production release and navi.zip packaged successfully!"
}
