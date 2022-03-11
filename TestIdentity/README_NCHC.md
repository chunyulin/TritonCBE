## Patatrack-AAS env at NCHC

- This note describes the native setup, instead of docker/singularity, for Patatrack-AAS both on TWCC x86 container and ARM-N1 bare metal node. 
- All is based on [Yongbin's this commit](https://github.com/yongbinfeng/TritonCBE/tree/42495f5ac14647c902cef48aa72dfeecfbd708af), except that I didn't compile those inside a container but only in a native Ubuntu 20.04 with CUDA enviorment and the latest version of cmake. 
- It turns out that the compilation even works on newer Triton version, say, 21.11. 

### Check all the repositories
```
export BASEDIR=`pwd`
git clone -b 21.02_phil_asynch_12_3_X_port git@github.com:yongbinfeng/identity_backend.git
git clone -b 21.02_phil_asynch_12_3_X_port git@github.com:yongbinfeng/pixeltrack-standalone.git
git clone git@github.com:yongbinfeng/TritonCBE.git
```

### Build Patatrack
```
cd pixeltrack-standalone
## sed -i "s/-msse3//g" Makefile   ### Need turn off sse3 on ARM
make -j cudadev
cd ..
```

### Build the backend
```
VER="r21.11"   ### Phil's backend seems to work even for new version of backend API... 
cd identity_backend

### The model path has been hard-wired, so ....
sed -i "s/\"\/models\//\"${BASEDIR//\//\\\/}\/TritonCBE\/TestIdentity\//g" src/loadbs.cc
sed -i "s/\"\/models\//\"${BASEDIR//\//\\\/}\/TritonCBE\/TestIdentity\//g" src/PluginManager.cc
sed -i "s/1\/plugins/1\/data\/plugins/g" src/PluginManager.cc

mkdir build
cd build
cmake -DTRITON_ENABLE_GPU=ON -DCMAKE_INSTALL_PREFIX:PATH=`pwd`/install \
      -DTRITON_BACKEND_REPO_TAG=${VER} -DTRITON_CORE_REPO_TAG=${VER} -DTRITON_COMMON_REPO_TAG=${VER} ..
make install
cd ../..
```

### Prepare model repository from the above
```
rsync -av $BASEDIR/pixeltrack-standalone/lib/cudadev/*.so \
          $BASEDIR/pixeltrack-standalone/external/tbb/lib/libtbb.so* \
          $BASEDIR/pixeltrack-standalone/external/libbacktrace/lib/libbacktrace.so* \
          $BASEDIR/identity_backend/build/libtriton_identity.so \
          $BASEDIR/TritonCBE/TestIdentity/identity_fp32/1/
cd ${BASEDIR}/TritonCBE/TestIdentity/identity_fp32/1/
curl https://www.dropbox.com/s/o91gcntmnizh54p/data.tar.gz?dl=0 -o /tmp/data_aas.tgz 
tar xzvf /tmp/data_aas.tgz
cp $BASEDIR/pixeltrack-standalone/data/beamspot.bin data/

cd ${BASEDIR}
```

### Lanuch the Triton server
```
docker run -idt --rm --name triton --gpus all -v $BASEDIR:$BASEDIR --net=host nvcr.io/nvidia/tritonserver:${BASEVER}-py3
cat <<EOF | docker exec -i triton /bin/bash
 export LD_LIBRARY_PATH=${BASEDIR}/TritonCBE/TestIdentity/identity_fp32/1:\${LD_LIBRARY_PATH}
 export LD_PRELOAD=libtbb.so.2:libFramework.so:libCUDACore.so:libCondFormats.so:libCUDADataFormats.so:pluginBeamSpotProd
 tritonserver --backend-config=tensorflow,version=2 --model-repository=${BASEDIR}/TritonCBE/TestIdentity
EOF
```
If the environment does not allow docker (like TWCC), just run the three line inside the cat/EOF. 

### Standalong test
The standalong test shows ~1.4k ev/s for 10 threads on V100, and slightly more on A100.
```
p00lcy01@gf0pobctr1646876496524-r2psv:/work/p00lcy01/PataTestNative/pixeltrack-standalone$ . ./do_test
Found 1 devices
reading gain.bin
reading SiPixelGainForHLTonGPU class fine; size 22320
reading nbytes fine; nbytes 3088384
reading gainData fine
 -gain- 3088384 -- 1.3834 -- -88
done reading gain.bin
reading fedIds
 nfeds 108fed ids
id 1200id 1201id 1202id 1203id 1204id 1205id 1206id 1207id 1208id 1209id 1212id 1213id 1214id 1215id 1216id 1217id 1218id 1219id 1220id 1221id 1224id 1225id 1226id 1227id 1228id 1229id 1230id 1231id 1232id 1233id 1236id 1237id 1238id 1239id 1240id 1241id 1242id 1243id 1244id 1245id 1248id 1249id 1250id 1251id 1252id 1253id 1254id 1255id 1256id 1257id 1260id 1261id 1262id 1263id 1264id 1265id 1266id 1267id 1268id 1269id 1272id 1273id 1274id 1275id 1276id 1277id 1278id 1279id 1280id 1281id 1284id 1285id 1286id 1287id 1288id 1289id 1290id 1291id 1292id 1293id 1296id 1297id 1298id 1299id 1300id 1301id 1302id 1308id 1309id 1310id 1311id 1312id 1313id 1314id 1320id 1321id 1322id 1323id 1324id 1325id 1326id 1332id 1333id 1334id 1335id 1336id 1337id 1338
done reading fedIds
reading cablingMap.bin
done reading cablingMap.bin
reading cpefast.bin
==> Filling CPE ./data/cpefast.bin
layer Geom ---> 16 --
---> 1092 -- 973 -- 116
--checking layer 1
--checking layer 2
done reading cpefast.bin
Processing 1000 events, of which 10 concurrently, with 10 threads.
Processed 1000 events in 7.109240e-01 seconds, throughput 1406.62 events/s.
```
Strangely, if running standalone test on ARM inside the container, the event rate drops to ~1 order smaller!
