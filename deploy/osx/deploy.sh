#!/bin/bash

# Execute this from the top-level directory of the project (the one
# that contains the .app bundle).  Supply the name of the .app bundle
# as argument (the target will use $app.app regardless, but we need
# to know the source)

source="$1"
dmg="$2"
if [ -z "$source" ] || [ ! -d "$source" ] || [ -z "$dmg" ]; then
	echo "Usage: $0 <source.app> <target-dmg-basename>"
	echo "  e.g. $0 MyApplication.app MyApplication"
 	echo "  Version number and .dmg will be appended automatically,"
        echo "  but the .app name must include .app"
	exit 2
fi
app=`basename "$source" .app`

version=`perl -p -e 's/^[^"]*"([^"]*)".*$/$1/' version.h`
case "$version" in
    [0-9].[0-9]) bundleVersion="$version".0 ;;
    [0-9].[0-9].[0-9]) bundleVersion="$version" ;;
    *) echo "Error: Version $version is neither two- nor three-part number" ;;
esac

if file "$source/Contents/MacOS/$app" | grep -q script; then
    echo
    echo "Executable is already a script, leaving it alone."
else
    echo
    echo "Moving aside executable, adding script."

    mv "$source/Contents/MacOS/$app" "$source/Contents/Resources/" || exit 1
    cp "deploy/osx/$app.sh" "$source/Contents/MacOS/$app" || exit 1
    chmod +x "$source/Contents/MacOS/$app"
fi

echo
echo "Copying in plugin."

cp ../yintony/yintony.{dylib,cat,n3} "$source/Contents/Resources/"

echo
echo "Fixing up paths."

deploy/osx/paths.sh "$app"

echo
echo "Making target tree."

volume="$app"-"$version"
target="$volume"/"$app".app
dmg="$dmg"-"$version".dmg

mkdir "$volume" || exit 1

ln -s /Applications "$volume"/Applications
cp README README.OSC COPYING CHANGELOG "$volume/"
cp -rp "$source" "$target"

echo "Done"

echo "Writing version $bundleVersion in to bundle."
echo "(This should be a three-part number: major.minor.point)"

perl -p -e "s/TONY_VERSION/$bundleVersion/" deploy/osx/Info.plist \
    > "$target"/Contents/Info.plist

echo "Done: check $target/Contents/Info.plist for sanity please"

deploy/osx/sign.sh "$volume" || exit 1

echo
echo "Making dmg..."

hdiutil create -srcfolder "$volume" "$dmg" -volname "$volume" && 
	rm -r "$volume"

echo "Done"
