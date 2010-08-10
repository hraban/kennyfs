mkdir -p mnt
cd ../
for f in test/*.ini
do
	echo -n "$f: "
	./kennyfs -d -o kfsconf="$f" mnt 2>&1 | cat > "$f.log"
	python detect_memleaks.py "$f.log" && rm -f "$f.log"
done
