#! /bin/sh

echo 'DatabaseName=Lyre'

for f in recipes/*; do
    . "$f"
    echo "[${name}]"
    ( . source-recipes/${from_source} && echo "Version=${version}r${revision}" )
    echo "Dependencies=${deps}"
    echo '[/]'
done
