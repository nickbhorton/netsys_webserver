mkdir -p temp
aria2c -j4 http://127.0.0.1:8888 -i files.txt -d temp --enable-http-keep-alive=true -l "aria2c.log"
rm -rf temp
ab -n 100 -c 5 http://localhost:8888/
