# Repository manager and build tool for Arch Linux

This **experimental** project contains **unofficial** tools to manage custom Arch Linux
repositories. It is built on top of the official tools provided by the `pacman` and
`devtools` packages.

At this point the project is rather raw and there are many things left to implement and
to improve (checkout the TODOs section below). Currently builds are exclusively triggered
manually (although the API brings the possibility for automation), scalability is limited
through the use of an in-memory database. Builds are only conducted on one host.

On the upside, this project can easily be used to together with other build scripts. It
doesn't care if your repository's DB file is updated by another application; just be sure
to reload the DB file (see Workflow section below). This should make an easy migration
path to use this for maintaining an existing repository.

So far this project consists of:

* libpkg: C++ library to parse Arch Linux packages and databases
    * parse pacman config
    * parse databases
    * extract and parse binary packages (.PKGINFO, dependencies on soname-level)
    * parse package source infos (.SRCINFO)
* librepomgr: C++ library for managing custom Arch Linux repositories
* srv: Daemon and web application for building Arch Linux packages and managing custom Arch
  Linux repositories
    * build packages via `makechrootpkg` and add them to a custom repository via `repo-add`
    * check AUR for package updates
    * build from locally stored PKGBUILDs and from the AUR
    * remove packages from a repository
    * move packages from one repository to another
    * check for repository issues (missing packages, package misses rebuild, …)
    * provides an HTTP API and a web UI with live streaming
* pacfind: Tool to find the package containing a certain file
    * requires `*.files`-databases to be present
    * does not need the full file path and supports regex (in contrast to `pacman -F`)
* cli: Command line tool to interact with srv
    * search for packages and show package details
    * show and submit build actions

Further ideas (not implemented yet):
* distri: Tool to distribute applications from the packages in a repository
    * bundle an application with its dependencies similar to `linuxdeployqt`
      and `windeployqt`

## Setup server
An example config files can be found in this repository, see the `srv/doc`
directory. The example config is also installed so one can easily get started.

This is an example for using the package
[`arch-repo-manager-git`](https://github.com/Martchus/PKGBUILDs/blob/master/arch-repo-manager/git/PKGBUILD):

```
mkdir -p /etc/buildservice-git
cp /usr/share/buildservice-git/skel/server-config-example.conf /etc/buildservice-git/server.conf
cp /usr/share/buildservice-git/skel/presets-example.json /etc/buildservice-git/presets.json
systemctl enable --now buildservice-git.service
```

Note that the `-git` package is used at this point because there are no releases yet.

### Setting up a working directory
The server needs a place to temporarily store PKGBUILDs, cache files and other stuff.
Just create a directory at any place with enough disk space and set the permissions so the
user you start the server with can read and write there. This directory goes under
`working_directory` in the settings file. Relative paths within the configuration file (used
for other locations) will be treated relative to that directory.

### Setting up a database/repository directories
The databases for the official repositories will be synced from the upstream repositories and
stored in a local directory. The actual packages might be found in the pacman cache directory.
Example configuration:

```
[database/core]
arch = x86_64
sync_from_mirror = on
dbdir = $sync_db_path/$arch
mirror = $default_mirror/$repo/os/$arch
```

Own repositories are not synced from a mirror and all packages are expected to be present in
a dedicated local directory. It makes most sense to store packages and databases in the same
directory. Example configuration:

```
[database/ownstuff]
path = $own_regular_db_path
files = $own_files_db_path
arch = x86_64
depends = core extra community multilib
pkgdir = $local_db_path/$repo/os/$arch
dbdir = $local_db_path/$repo/os/$arch
mirror = $local_mirror/$repo/os/$arch
```

The placeholder/variable values come from the previous "definitions" block, e.g.:

```
[definitions]
sync_db_path = sync-dbs
local_db_path = /run/media/repo/arch
own_sync_db_path = $local_db_path/$repo/os/$arch
own_regular_db_path = $local_db_path/$repo/os/$arch/$repo.db.tar.xz
own_files_db_path = $local_db_path/$repo/os/$arch/$repo.files.tar.xz
default_mirror = http://mirror.23media.de/archlinux
local_mirror = file://$local_db_path
```

The server obviously needs write permissions to add packages to repositories. In my
setup I've just add it as group and set permissions accordingly:

```
sudo chown -R martchus:buildservice $local_db_path
find $local_db_path -type d -exec chmod 775 {} \+
find $local_db_path -type f -exec chmod 664 {} \+
```

### Setting up the chroot for the build
The overall reasoning and procedure is already outlined
[in the Wiki](https://wiki.archlinux.org/index.php/DeveloperWiki:Building_in_a_clean_chroot).

I also like to be able to build for other architectures than x86_64 so the server
expects a directory layout like this:

```
/the/chroot/directory
├── arch-i686
│   └── root
├── arch-x86_64
│   └── root
├── config-i686
│   ├── makepkg.conf
│   ├── pacman.conf
├── config-x86_64
│   ├── makepkg.conf
│   └── pacman.conf
```

So there's a "root" chroot for every architecture and config files for that architecture
in dedicated directories. To achieve this one just has to call `mkarchroot` for each architecture.
It is invoked like this:

```
mkarchroot -C /path/to/pacman.conf -M /path/to/makepkg.conf /directory/to/store/the/tree base-devel
```

Note that `makechrootpkg` will not use the "root" chroot directories directly. It will create a
working copy using `rsync -a --delete -q -W -x "$chrootdir/root/" "$copydir"` (when using btrfs
it creates a subvolume instead of using rsync).

When following the procedure the "root" chroot is owned by `root` and the working copies by the
user you start the buildservice/makechrootpkg with.

Until I find a better solution the buildservice updates the config files within the "root" chroot
to configure the databases used during the build. It relies on the configuration files stored within
the `config-*` directories for that and needs permissions to update them:

```
sudo chown buildservice:buildservice /the/chroot/directory/arch-x86_64/root/etc/{pacman,makepkg}.conf
```

### sudo configuration
`makechrootpkg` seems to use `sudo` internally so it is required that the user one starts the
server with is allowed to use `sudo`. Currently it is not possible to supply the password
automatically so one has to allow access without password like this:

```
buildservice ALL=(ALL) NOPASSWD:ALL
```

### Sample NGINX config
To make the server publicy accessible one should use a reverse proxy. NGINX example:

```
proxy_http_version 1.1; # this is essential for chunked responses to work
proxy_read_timeout 1d; # prevent timeouts when serving live stream

location /buildservice/api/ {
    proxy_buffering         off; # for live streaming of logfiles
    proxy_pass              http://localhost:8090/api/;
    proxy_set_header        Host $host;
    proxy_set_header        X-Real-IP $remote_addr;
    proxy_set_header        X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header        X-Forwarded-Proto $scheme;
}
location ~ ^(?!/buildservice/api/)/buildservice/(.*)$ {
    alias /run/media/devel/projects/c++/cmake/auto-makepkg/buildservice/static/$1;
}

```

### Sample GPG config
This minimal GPG config allows `makepkg` to validate signatures which might be present
in some PKGBUILDs:
```
~/.gnupg/gpg.conf
---
utf8-strings
keyring /etc/pacman.d/gnupg/pubring.gpg
keyserver-options auto-key-retrieve
keyserver hkp://keys.gnupg.net
```

#### Notes
* Adding the pacman keyring is actually not very useful because we need to check signatures
  of any upstream project and not just arch devs.
* If "auto-key-retrieve" does not work, use `gpg --recv-key <KEYID>` as a workaround
* Also see http://allanmcrae.com/2015/01/two-pgp-keyrings-for-package-management-in-arch-linux/

### ccache configuration
To use ccache one can set the ccache directory in the config (`ccache_dir`) and install the
package `ccache` (and `mingw-w64-ccache` for MinGW packages) into the chroot. Make sure the user
you start the server with has permissions to read and write there. Otherwise the resulting
configure errors can be confusing. Internally the server is mounting that directory like
described in [the wiki](https://wiki.archlinux.org/index.php/Ccache#makechrootpkg).

If you want to use the existing `ccache` directory owned by your current user, you could do
the following to grant the `buildservice` user access to it:

1. Add new group for accessing the cache and add the users to it:
   `sudo groupadd ccache`, `sudo usermod -a -G ccache "$USER"`, `sudo usermod -a -G ccache buildservice`
2. Set group ownership: `sudo chown -R $USER:ccache $ccache_dir`
3. Ensure dirs are readable by the group and that the group is inherited:
   `sudo find "$ccache_dir" -type d -exec chmod 2775 {} \+`
4. Ensure that all files are readable by the group:
   `sudo find "$ccache_dir" -type f -exec chmod 664 {} \+`
5. Add `umask = 002` to the ccache config so new files/dirs created by it are fully accessible by
   the group.
6. Patch `devtools` so additional groups are configured within the container (see
   https://github.com/Martchus/devtools/commit/b981c6afe81219ff4ca1ea34d0be5cc681408962)


Note that ccache *slows* down the initial compilation. It is only useful to enable it if rebuilds
with only slight changes are expected. Currently it is *not* possible to enable/disable ccache usage
per package (e.g. via a regex).

## Workflow
So called "build actions" are used to conduct certain tasks, e.g. checking for updates or problems,
moving packages from one repository to another, preparing the build and conducting the build. The web
UI contains a form to create a new build action and shows the possible options.

It would be tedious to create the same set of build actions over and over again. Hence it is also
possible to create "pre defined tasks". Such a task contains a list of build actions with pre-filled
flags and settings. By default, the build actions within a task run one after another but it is also
possible to specify that certain build actions can run in parallel. The presets are configured by
editing the presets JSON file (e.g. `/etc/buildservice-git/presets.json` in the example config).

### General
* So far databases are *not* automatically reloaded. Use the "Reload database" build action to reload
  one or more databases after databases have been changes by build actions or manually. This build
  action is similar to `pacman -Sy`.
* Use the "Reload configuration" build action to apply configuration changes without restarting the
  service.

### Building packages
0. Ensure the databases are up to date via the "Reload databases" build action.
1. Start the "Prepare building" build action.
    * Specify the names of packages to be built.
        * If a package already exists the `pkgrel` or `epoch` is bumped as needed so the rebuilt
          packages are considered as newer by pacman. A warning is logged when bumping is done.
        * Missing dependencies are pulled into the build automatically. Whether dependencies are
          considered "missing" or "given" depends on the specified databases (see next points).
        * Packages will be split into batches. The first batch contains all packages which can be
          built immediately. The second batch contains all packages which only depend on packages in
          the first batch. The third batch contains all packages which only depend on packages in the
          second batch and so on. That means packages do not need to be specified in the correct order.
          However, the order in which packages are specified is preserved within each batch so it makes
          still sense to specify packages in the order you would prefer to build them.
        * Cyclic dependencies can not be added to a batch. A list of cyclic "leftovers" is emitted
          if those exist and that is considered a failure. If this is the case you need to add a
          bootstrap package to break the cycle. The build system is not clever enough to pull a bootstrap
          package automatically into the build so it must be specified explicitly. E.g. to build
          `mingw-w64-freetype2` and `mingw-w64-harfbuzz` one needs to add `mingw-w64-freetype2-bootstrap`
          explicitly to the list of packages to be built.
    * Specify exactly one destination database. The built packages are added to this database.
    * Specify source databases.
        * All packages contained by the source databases are considered as "given" and not pulled
          into the build as dependency.
        * If no source databases are specified the destination database and all databases the
          destination database is based on are used as source databases.
        * Source databases must be the destination database or a database the destination database
          is based on.
    * Specify a "directory" to store meta-data, logs, PKGBUILDs, sources and packages.
    * Check "Clean source directories" when previously prepared sources which are still present in
      the specified "directory" should be overridden.
    * When the build action has finished, have a look at the results to check whether they are as
      expected. If not, just restart the build action with adjusted parameters. The "directory"
      can generally be re-used. Use "Clean source directories" as needed. To override only a single
      source directory, simply delete it manually before restarting the build action.
    * You can amend PKGBUILDs created in the "directory" as needed.
2. Start the "Conduct building" build action.
    * Specify the same "directory" as in 1.
    * Do *not* specify any databases. The databases as specified in 1. will be used.
    * One can optionally specify package names to build only a subset of the initially prepared
      build.
    * When starting the build, the following steps are performed:
        1. All sources of all packages are downloaded. The build is stopped when not all sources
           can be downloaded.
        2. Packages will now be built in the same order as computed when preparing. Building of
           the next batch is stopped when a failure happened within the previous batch.
    * When using "Build as far as possible" the build is not stopped as previously explained.
    * When using "Save chroot of failures" the chroot directory is renamed when a build failure
      happens and therefore not overridden by the next build. This is useful to investigate the
      failure later without starting the build from scratch.
    * When the build has been stopped due to failures one can simply restart the build action. Packages
      which have already been built will be skipped.
        * It is useful to modify `build-progress.json` in the "directory".
            * Set `finished` back to `"1"` and `addedToRepo` back to `false` to let the build system think
              a package has not been built yet. This is useful to build a package again after a non-fatal
              packaging mistake.
            * Set `addedToRepo` to `true` let the build system  think a package has already been built.
              This effectively skips a package completely.
            * Set `hasSources` back to `false` to recreate the source package and download sources
              again as needed. This will also copy over contents of the `src` directory to the `pkg`
              directory again.
3. Amend a PKGBUILD file to fix a failure within a "big rebuild".
    * Fix the package at the source, e.g. the AUR or the local package directory, then
        1. Clean the `src` directory within "directory" for the package(s) or use "Clean source directory" if
           all packages need fixing.
        2. Restart step 1. Existing sources are reused (except for the removed `src` directories) so it shouldn't
           take long. The build progress (`hasSources` in particular) of the affected package(s) should be reset.
        3. Restart step 2.
    * or: Amend the PKGBUILD within the `src` directory.
        1. Set `hasSources` back to `false` as mentioned under 2. to retrigger building the source directory.
        2. Optionally restart step 1. to reevaluate possibly adjusted versions, split packages and dependencies.
           Be sure *not* to check "Clean source directory" as this would override your amendment.
        2. Restart step 2.

#### Further notes
* One can keep using the same "directory" for different builds. Since the `pkg` dirs will not be cleaned
  up (existing files are only updated/overridden) previously downloaded source files can be reused (useful if
  only `pkgrel` changes or when building from VCS sources or when some sources just remain the same).

## TODOs and further ideas for improvement
* [ ] Use persistent, non-in-memory database (e.g. lmdb or leveldb) to improve scalability
* [ ] Allow triggering tasks automatically/periodically
* [ ] Allow to run `makechrootpkg` on a remote host (e.g. via SSH) to work can be spread across multiple hosts
* [ ] More advanced search options
* [ ] Refresh build action details on the web UI automatically while an action is pending
* [ ] Fix permission error when stopping a process
* [ ] Keep the possibility for a "soft stop" where the build action would finish the current item
* [ ] Show statistics like CPU and RAM usage about ongoing build processes
* [ ] Stop a build process which doesn't produce output after a certain time
* [ ] Find out why the web service sometimes gets stuck
    * Restarting the client (browser) helps in most cases, it likely just hits
      [a limitation to the maximum number of open connections](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events#receiving_events_from_the_server)
    * Add "stress" test for live-streaming
        * Start process producing lots of output very fast
        * Let different clients connect and disconnect fast
* [ ] Improve test coverage
* [ ] Add fancy graphs for dependencies on the web UI
* [ ] Include `xterm.js` via JavaScript modules (blocked by https://github.com/xtermjs/xterm.js/issues/2878)

## Build instructions and dependencies
For a PKGBUILD checkout my [PKGBUILDs repository](https://github.com/Martchus/PKGBUILDs).

### C++ stuff
The application depends on [c++utilities](https://github.com/Martchus/cpp-utilities),
[reflective-rapidjson](https://github.com/Martchus/reflective-rapidjson), some Boost modules and OpenSSL.

For basic instructions checkout the README file of [c++utilities](https://github.com/Martchus/cpp-utilities).

### Web stuff
The only dependency is `xterm.js` which is bundled. However, only the JavaScript and CSS files are
bundled. For development the full checkout might be useful (e.g. for TypeScript mapping). It can be
retrieved using npm:

```
cd srv/static/node_modules
npm install xterm
npm install --save xterm-addon-search
```
