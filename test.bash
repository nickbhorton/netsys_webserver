mkdir -p temp
aria2c --enable-http-pipelining=true http://127.0.0.1:8888 -i files.txt -d temp 
rm -rf temp
