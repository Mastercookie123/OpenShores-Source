#include "filament_api.h"
#include <windows.h>
#include <vector>
#include <cstdio>
#include <cmath>
int main(int argc,char**argv){
 if(argc!=3)return 2;auto dll=LoadLibraryA(argv[1]);if(!dll){printf("LoadLibrary failed: %lu\n",GetLastError());return 3;}
 auto create=reinterpret_cast<decltype(&of_create)>(GetProcAddress(dll,"of_create"));
 auto draw=reinterpret_cast<decltype(&of_draw)>(GetProcAddress(dll,"of_draw"));
 auto destroy=reinterpret_cast<decltype(&of_destroy)>(GetProcAddress(dll,"of_destroy"));
 auto error=reinterpret_cast<decltype(&of_error)>(GetProcAddress(dll,"of_error"));
 if(!create||!draw||!destroy||!error)return 4;void* context=create(argv[2]);if(!context){printf("%s\n",error());return 5;}
 OFVertex vertices[3]={};float positions[3][3]={{-.8f,-.8f,0},{-.8f,.8f,0},{.8f,-.8f,0}};
 for(int i=0;i<3;i++){for(int k=0;k<3;k++)vertices[i].position[k]=positions[i][k];vertices[i].normal[2]=1;vertices[i].weights[0]=1;vertices[i].top=1;}
 uint8_t red[]={255,0,0,255};OFMesh mesh={};mesh.key=1;mesh.vertices=vertices;mesh.vertexCount=3;mesh.skin={1,1,1,red};mesh.roughness=.7f;
 OFFrame frame={};frame.abiVersion=OF_ABI_VERSION;frame.width=frame.height=256;frame.meshCount=1;frame.meshes=&mesh;
 for(int i=0;i<16;i++)frame.projection[i]=frame.modelView[i]=i%5==0?1.f:0.f;
 for(int b=0;b<OF_BONES;b++)for(int i=0;i<16;i++)frame.bones[b*16+i]=i%5==0?1.f:0.f;
 frame.ambient[0]=frame.ambient[1]=frame.ambient[2]=1;
 std::vector<uint8_t> color(256*256*4);std::vector<float> depth(256*256);
 int result=draw(context,&frame,color.data(),depth.data());if(!result){printf("%s\n",error());destroy(context);return 6;}
 int index=100*256+48;printf("sample RGBA %u %u %u %u depth %.9f\n",color[index*4],color[index*4+1],color[index*4+2],color[index*4+3],depth[index]);
 bool ok=color[index*4]>5 && color[index*4]>color[index*4+1] && std::abs(depth[index]-.5f)<1e-5f;
 int background=230*256+230;ok=ok&&color[background*4+3]==0&&std::abs(depth[background]-1.f)<1e-5f;
 // Asymmetric sample catches vertical inversion independently of the compositor.
 printf("orientation lower=%u upper=%u\n",color[(40*256+180)*4+3],color[(215*256+180)*4+3]);
 ok=ok&&color[(40*256+180)*4+3]>0&&color[(215*256+180)*4+3]==0;
 // Update only the original bone matrix: no geometry re-upload / animation clip.
 frame.bones[14]=.4f;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false;
 printf("animated depth %.9f\n",depth[index]);ok=ok&&std::abs(depth[index]-.7f)<1e-5f;
 // Clothing replaces skin through the game's wear flag; cache uses distinct keys.
 uint8_t blue[]={0,0,255,255};mesh.clothes={2,1,1,blue};mesh.clothesOffsetWear[2]=1;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false;
 printf("clothing R=%u B=%u\n",color[index*4],color[index*4+2]);ok=ok&&color[index*4+2]>color[index*4];
 // Resize and switch to a real perspective camera; z=-2 maps to 0.55555.
 frame.width=320;frame.height=192;color.resize(320*192*4);depth.resize(320*192);
 for(float& x:frame.projection)x=0;frame.projection[0]=frame.projection[5]=1;frame.projection[10]=-11.f/9;frame.projection[11]=-1;frame.projection[14]=-20.f/9;frame.bones[14]=-2;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false;
 index=80*320+130;printf("perspective depth %.9f\n",depth[index]);ok=ok&&std::abs(depth[index]-5.f/9)<1e-5f;
 auto release=reinterpret_cast<decltype(&of_release)>(GetProcAddress(dll,"of_release"));if(release)release(context,1);else ok=false;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false; // same mesh after release must rebuild
 // Real shadow regression: an offset point light and a blocker must darken a
 // separate receiver. Comparing shadow OFF/ON excludes baked texture shading.
 auto plane=[](float x0,float y0,float x1,float y1,float z){
  std::vector<OFVertex> v(6);float xy[6][2]={{x0,y0},{x0,y1},{x1,y0},{x0,y1},{x1,y1},{x1,y0}};
  for(int i=0;i<6;i++){v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[2]=z;v[i].normal[2]=1;v[i].weights[0]=1;v[i].top=1;}return v;
 };
 auto receiver=plane(-3,-2,3,2,-5),blocker=plane(-.5f,-.5f,.5f,.5f,-3);
 uint8_t white[]={255,255,255,255};OFMesh scene[2]={};
 for(int i=0;i<2;i++){scene[i].key=10+i;scene[i].skin={10,1,1,white};scene[i].roughness=.8f;scene[i].vertexCount=6;scene[i].vertices=i?blocker.data():receiver.data();}
 frame.meshCount=2;frame.meshes=scene;frame.bones[14]=0;for(float& x:frame.ambient)x=0;
 OFLight lamp={{2,1,-1},{1,1,1},6000000,0};frame.lightCount=1;frame.lights=&lamp;frame.shadowLights=0;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false;
 auto save=[&](const char* name){FILE* file=nullptr;fopen_s(&file,name,"wb");if(!file)return;fprintf(file,"P6\n320 192\n255\n");for(int y=191;y>=0;y--)for(int x=0;x<320;x++)fwrite(&color[(y*320+x)*4],1,3,file);fclose(file);};
 save("filament-shadow-off.ppm");int sample=(76*320+96)*4;int lit=color[sample];frame.shadowLights=1;
 if(!draw(context,&frame,color.data(),depth.data()))ok=false;
 save("filament-shadow-on.ppm");int shaded=color[sample];printf("point shadow receiver off=%d on=%d\n",lit,shaded);ok=ok&&lit>30&&shaded<lit/2;
 destroy(context);FreeLibrary(dll);printf("%s native C ABI color/depth/live-bones/clothing/resize/release/point-shadow test\n",ok?"PASS":"FAIL");return ok?0:7;
}
