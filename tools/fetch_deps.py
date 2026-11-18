#!/usr/bin/env python3
"""Fetch Google Play Review SDK dependencies for Android in V / Velo games.

Usage:
    python3 tools/fetch_deps.py [android|all] [--project <path>]

Android:
    Downloads Google Play Review SDK AAR/JAR dependencies into android/deps/
    and installs the RatingBridge.java glue code.
"""
import argparse
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODULE_ROOT = os.path.dirname(SCRIPT_DIR)

REPOS = [
    'https://dl.google.com/dl/android/maven2',
    'https://repo.maven.apache.org/maven2'
]

DEFAULT_CONFIG = {
    'dependencies': ['com.google.android.play:review:2.0.2']
}


def fetch_file(url, cache_dir):
    os.makedirs(cache_dir, exist_ok=True)
    filename = re.sub(r'[^A-Za-z0-9._-]', '_', url)
    dest = os.path.join(cache_dir, filename)
    if os.path.isfile(dest):
        return dest
    print(f'Downloading {url} ...')
    part = dest + '.part'
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'VeloRating/1.0'})
        with urllib.request.urlopen(req, timeout=120) as r, open(part, 'wb') as f:
            shutil.copyfileobj(r, f)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            if os.path.exists(part):
                os.remove(part)
            return None
        raise
    os.replace(part, dest)
    return dest


def repo_file(group, artifact, version, ext, cache_dir):
    rel = f"{group.replace('.', '/')}/{artifact}/{version}/{artifact}-{version}.{ext}"
    for repo in REPOS:
        p = fetch_file(f'{repo}/{rel}', cache_dir)
        if p:
            return p
    return None


def strip_ns(root):
    for el in root.iter():
        if '}' in el.tag:
            el.tag = el.tag.split('}', 1)[1]
    return root


class Pom:
    def __init__(self, group, artifact, version, cache_dir):
        self.group, self.artifact, self.version, self.cache_dir = group, artifact, version, cache_dir
        path = repo_file(group, artifact, version, 'pom', cache_dir)
        if not path:
            raise SystemExit(f'no POM for {group}:{artifact}:{version}')
        self.root = strip_ns(ET.parse(path).getroot())
        self.parent = None
        p = self.root.find('parent')
        if p is not None:
            self.parent = Pom(p.findtext('groupId'), p.findtext('artifactId'), p.findtext('version'), cache_dir)
        self.props = dict(self.parent.props) if self.parent else {}
        props = self.root.find('properties')
        if props is not None:
            for el in props:
                self.props[el.tag] = (el.text or '').strip()
        self.props['project.version'] = self.root.findtext('version') or (self.parent.version if self.parent else version)
        self.props['project.groupId'] = self.root.findtext('groupId') or (self.parent.group if self.parent else group)
        self.managed = dict(self.parent.managed) if self.parent else {}
        for d in self.root.findall('dependencyManagement/dependencies/dependency'):
            v = d.findtext('version')
            if v:
                self.managed[(d.findtext('groupId'), d.findtext('artifactId'))] = self.sub(v)
        self.packaging = (self.root.findtext('packaging') or 'jar').strip()

    def sub(self, s):
        if not s:
            return ''
        for _ in range(5):
            m = re.search(r'\$\{([^}]+)\}', s)
            if not m:
                break
            k = m.group(1)
            v = self.props.get(k, '')
            s = s[:m.start()] + v + s[m.end():]
        return s

    def runtime_deps(self):
        deps = []
        for d in self.root.findall('dependencies/dependency'):
            scope = (d.findtext('scope') or 'compile').strip()
            if scope not in ('compile', 'runtime'):
                continue
            if (d.findtext('optional') or '').strip().lower() == 'true':
                continue
            g = self.sub(d.findtext('groupId'))
            a = self.sub(d.findtext('artifactId'))
            v = self.sub(d.findtext('version')) or self.managed.get((g, a), '')
            if not v:
                continue
            deps.append((g, a, v))
        return deps


def resolve(root_coords, cache_dir):
    resolved, seen = [], set()

    def walk(coord):
        g, a, v = coord
        key = (g, a)
        if key in seen:
            return
        seen.add(key)
        try:
            pom = Pom(g, a, v, cache_dir)
        except SystemExit as e:
            print(f'Warning: {e}')
            return
        for dep in pom.runtime_deps():
            walk(dep)
        resolved.append((g, a, v, pom.packaging))

    for c in root_coords:
        walk(c)
    return resolved


def merge_manifest(manifests):
    perms, app_children = [], []
    for mf in manifests:
        if not os.path.isfile(mf):
            continue
        try:
            root = ET.parse(mf).getroot()
        except ET.ParseError:
            continue
        for p in root.findall('uses-permission'):
            perms.append(ET.tostring(p, encoding='unicode'))
        for p in root.findall('uses-permission-sdk-23'):
            perms.append(ET.tostring(p, encoding='unicode'))
        app = root.find('application')
        if app is not None:
            for child in list(app):
                app_children.append(ET.tostring(child, encoding='unicode'))
    return ''.join(perms), ''.join(app_children)


def setup_android(project_root):
    print('=== Setting up Google Play Review SDK for Android ===')
    cache_dir = os.path.join(project_root, 'android', '.cache')
    deps_dir = os.path.join(project_root, 'android', 'deps')
    jars_dir = os.path.join(deps_dir, 'jars')
    res_dir = os.path.join(deps_dir, 'res')
    os.makedirs(jars_dir, exist_ok=True)
    os.makedirs(res_dir, exist_ok=True)

    cfg_file = os.path.join(project_root, 'android', 'deps.json')
    module_cfg = os.path.join(MODULE_ROOT, 'android', 'deps.json')
    cfg = DEFAULT_CONFIG.copy()
    if os.path.isfile(cfg_file):
        with open(cfg_file) as f:
            cfg.update(json.load(f))
    elif os.path.isfile(module_cfg):
        with open(module_cfg) as f:
            cfg.update(json.load(f))

    roots = []
    for dep in cfg.get('dependencies', []):
        parts = dep.split(':')
        if len(parts) == 3:
            roots.append(tuple(parts))

    print(f'Resolving Maven dependencies for: {roots}')
    artifacts = resolve(roots, cache_dir)
    print(f'Resolved {len(artifacts)} libraries')

    manifests = []
    packages = []

    for g, a, v, pkg in artifacts:
        tag = f'{g}_{a}_{v}'
        ext = 'aar' if pkg == 'aar' else 'jar'
        src_path = repo_file(g, a, v, ext, cache_dir)
        if not src_path:
            continue

        if ext == 'jar':
            shutil.copy(src_path, os.path.join(jars_dir, f'{tag}.jar'))
            continue

        with zipfile.ZipFile(src_path) as z:
            names = z.namelist()
            if 'classes.jar' in names:
                with z.open('classes.jar') as c_src, open(os.path.join(jars_dir, f'{tag}.jar'), 'wb') as c_dst:
                    shutil.copyfileobj(c_src, c_dst)

            res = [x for x in names if x.startswith('res/') and not x.endswith('/')]
            if res:
                r_root = os.path.join(res_dir, tag)
                for x in res:
                    dst_path = os.path.join(r_root, x[4:])
                    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
                    with z.open(x) as s, open(dst_path, 'wb') as d:
                        shutil.copyfileobj(s, d)

            if 'AndroidManifest.xml' in names:
                mf = os.path.join(cache_dir, f'manifest_{tag}.xml')
                with z.open('AndroidManifest.xml') as s, open(mf, 'wb') as d:
                    shutil.copyfileobj(s, d)
                manifests.append(mf)
                try:
                    p = ET.parse(mf).getroot().get('package')
                    if p:
                        packages.append(p)
                except Exception:
                    pass

    perms, application = merge_manifest(manifests)
    perms = ''.join(dict.fromkeys(perms.splitlines(True)))

    manifest_perms_file = os.path.join(deps_dir, 'manifest_permissions.xml')
    manifest_app_file = os.path.join(deps_dir, 'manifest_application.xml')

    existing_perms = open(manifest_perms_file).read() if os.path.isfile(manifest_perms_file) else ''
    existing_app = open(manifest_app_file).read() if os.path.isfile(manifest_app_file) else ''

    all_perms = ''.join(dict.fromkeys((existing_perms + perms).splitlines(True)))
    with open(manifest_perms_file, 'w') as f:
        f.write(all_perms)
    if application and application not in existing_app:
        with open(manifest_app_file, 'w') as f:
            f.write(existing_app + application)

    extra_pkg_file = os.path.join(deps_dir, 'extra_packages.txt')
    existing_pkgs = open(extra_pkg_file).read().splitlines() if os.path.isfile(extra_pkg_file) else []
    merged_pkgs = sorted(set(existing_pkgs + packages))
    if merged_pkgs:
        with open(extra_pkg_file, 'w') as f:
            f.write('\n'.join(merged_pkgs) + '\n')

    # Install RatingBridge.java
    module_bridge = os.path.join(MODULE_ROOT, 'android', 'RatingBridge.java')
    target_bridge_dir = os.path.join(deps_dir, 'src', 'io', 'velo', 'rating')
    os.makedirs(target_bridge_dir, exist_ok=True)
    if os.path.isfile(module_bridge):
        shutil.copy(module_bridge, os.path.join(target_bridge_dir, 'RatingBridge.java'))
        print(f'Installed RatingBridge.java -> {target_bridge_dir}')

    print(f'Android Google Play Review setup complete -> {os.path.relpath(deps_dir, project_root)}')


def main():
    parser = argparse.ArgumentParser(description='Fetch Play Review dependencies for Android')
    parser.add_argument('platform', nargs='?', default='all', choices=['android', 'all'])
    parser.add_argument('--project', default=os.getcwd(), help='Root path of the game project')
    args = parser.parse_args()

    project_root = os.path.abspath(args.project)
    if args.platform in ('android', 'all'):
        setup_android(project_root)


if __name__ == '__main__':
    main()
