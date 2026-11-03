"""Translate the game's material/vertex math, replacing only its lighting.

Original GLSL remains the source of UVs, palettes, procedural assembly, wind,
terrain blends and alpha tests. Filament supplies the shared PBR light/shadow
pipeline. A generated manifest is also the runtime uniform/attribute contract.
"""
import argparse,json,re,subprocess,hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
SHADERS=ROOT/'src/lib/graphics/resources/shaders'
DECL=re.compile(r'\buniform\s+(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;')
IO=re.compile(r'(?m)^\s*(?:(flat|smooth|noperspective)\s+)?(in|out)\s+(\w+)\s+(\w+)\s*;')
SIZES={'float':1,'int':1,'uint':1,'bool':1,'vec2':2,'vec3':3,'vec4':4,'ivec2':2,'ivec3':3,'ivec4':4,'mat2':4,'mat3':9,'mat4':16}
TYPES={'vec2':'float2','vec3':'float3','vec4':'float4','ivec2':'int2','ivec3':'int3','ivec4':'int4','mat2':'mat2','mat3':'mat3','mat4':'mat4'}

def preprocess(path):
    source=path.read_text(encoding='utf-8-sig')
    for header in SHADERS.glob('H_*.glsl'):
        source=re.sub(r'(?m)^\s*#'+re.escape(header.stem[2:])+r'\b[^\n]*',lambda _:header.read_text(),source)
    source=re.sub(r'(?m)^\s*#(?:version|extension|pragma).*$', '',source)
    result=subprocess.run(['cpp','-P','-undef','-DMAX_LIGHTS=4','-DMAX_BONES=61','-DMAX_ANIMAL_BONES=50','-DMaxBones=61','-DMAX_CLOTHING_TEXTURES=15'],input=source,text=True,capture_output=True)
    if result.returncode:raise ValueError(result.stderr)
    return result.stdout

def functions(source):
    result=[]
    for match in re.finditer(r'(?m)^\s*(?:void|float|int|bool|vec[234]|mat[234])\s+(\w+)\s*\([^;{}]*\)\s*\{',source):
        start=match.start();end=match.end();depth=1
        while depth and end<len(source):
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        result.append((match.group(1),start,end,source[start:end]))
    return result

def unlight(source):
    for name,start,end,body in reversed(functions(source)):
        if name=='computeLighting':
            brace=body.index('{');source=source[:start]+body[:brace]+'{ return vec3(1.0); }'+source[end:]
    # Remove unreachable lighting/shadow helpers before enumerating uniforms.
    funcs=functions(source);reachable={'main'};changed=True
    while changed:
        changed=False
        for name,_,_,body in funcs:
            if name in reachable:
                for candidate,_,_,_ in funcs:
                    if candidate not in reachable and re.search(r'\b'+candidate+r'\s*\(',body):reachable.add(candidate);changed=True
    for name,start,end,body in reversed(funcs):
        if name not in reachable:source=source[:start]+source[end:]
    # Fog belongs after lighting. Native view fog replaces these old mixes.
    source=re.sub(r'fragColor\.rgb\s*=\s*mix\(fragColor\.rgb,\s*fogColor,\s*[^;]+;', '',source)
    source=re.sub(r'fragColor\s*=\s*mix\(fragColor,\s*vec4\(fogColor,\s*1\),\s*([^;]+)\);', r'fragColor.a=mix(fragColor.a,1.0,\1);',source)
    return source

def unique_locals(source):
    for name,start,end,body in reversed(functions(source)):
        locals=set(re.findall(r'\b(?:const\s+)?(?:float|int|uint|bool|[ib]?vec[234]|mat[234]|LightSources|PointLight|SunLight)\s+(\w+)\s*(?=[=;,\[)])',body))
        for variable in sorted(locals,key=len,reverse=True):
            body=re.sub(r'(?<![.\w])'+re.escape(variable)+r'\b','osl_'+name+'_'+variable,body)
        source=source[:start]+body+source[end:]
    return source

def replace_calls(source,function,callback):
    pattern=re.compile(r'\b'+function+r'\s*\(');cursor=0
    while True:
        match=pattern.search(source,cursor)
        if not match:return source
        end=match.end();depth=1;start=end;args=[]
        while depth:
            ch=source[end]
            if ch=='(':depth+=1
            elif ch==')':depth-=1
            if (ch==',' and depth==1) or depth==0:args.append(source[start:end].strip());start=end+1
            end+=1
        replacement=callback(args)
        if replacement is None:cursor=end;continue
        source=source[:match.start()]+replacement+source[end:];cursor=match.start()+len(replacement)

def convert(vname,fname,output):
    raw_v=preprocess(SHADERS/(vname+'.glsl'));raw_f=preprocess(SHADERS/(fname+'.glsl'))
    fog=re.search(r'fragColor\.rgb\s*=\s*mix\(fragColor\.rgb,\s*fogColor,\s*([^;]+)\);',raw_f)
    if not fog:fog=re.search(r'fragColor\s*=\s*mix\(fragColor,\s*vec4\(fogColor,\s*1\),\s*([^;]+)\);',raw_f)
    fog_factor=fog[1] if fog else None
    v=unlight(raw_v);f=unlight(raw_f)
    lit='computeLighting(' in v
    # Their old tangent-space sun/specular calculation is replaced by PBR.
    # Keep the preceding hole/clip tests and all original vertex wave motion.
    liquid=vname.startswith(('Water','Lava'))
    if liquid:
        main=next(item for item in functions(f) if item[0]=='main')
        body=main[3];bump=body.find('vec3 normDelta')
        if bump>=0:
            brace=body.rfind('{',0,bump)
            color='waterColor' if vname.startswith('Water') else 'lavaColor'
            alpha=('opacity' if 'NoHole' in vname else 'opacity * clipAlpha') if color=='waterColor' else '1.0'
            f=f[:main[1]]+body[:brace]+f'fragColor=vec4({color},{alpha});}}'+f[main[2]:]
            lit=True
    attrs=[{'name':name,'type':typ,'size':SIZES[typ]} for _,kind,typ,name in IO.findall(v) if kind=='in']
    varyings=[{'name':name,'type':typ,'size':SIZES[typ]} for _,kind,typ,name in IO.findall(f) if kind=='in' and (len(re.findall(r'\b'+name+r'\b',f))>1 or fog_factor and re.search(r'\b'+name+r'\b',fog_factor))]
    if len(attrs)>8:raise ValueError(f'{len(attrs)} attributes exceed native contract')
    offset=0
    for varying in varyings:varying['offset']=offset;offset+=varying['size']
    # Point sprites get a procedural quad coordinate; no GL geometry shader.
    points='gl_PointSize' in v or 'gl_PointCoord' in f
    if points:point_offset=offset;offset+=2
    self_illum=any(a['name']=='glowSelfIllum' for a in attrs)
    if self_illum:self_offset=offset;offset+=1
    if offset>20:raise ValueError(f'{offset} varying components exceed Filament limit')
    uniforms={}
    for source in (v,f):
        for typ,name,count in DECL.findall(source):
            if typ=='LightSources':continue
            if typ=='samplerBuffer':continue
            if typ not in SIZES and not typ.startswith('sampler'):raise ValueError(f'Unknown uniform type {typ}')
            uniforms[name]={'name':name,'type':typ,'count':int(count or 1),'parameter':'os_'+name}
    for source in (v,f):
        for typ,name,count in DECL.findall(source):
            if typ=='samplerBuffer' and name!='instances':raise ValueError(f'Unimplemented buffer sampler {name}')
    params=[]
    for uniform in uniforms.values():
        typ=uniform['type'];count=uniform['count'];p=uniform['parameter']
        if typ.startswith('sampler'):
            if typ not in ('sampler2D','samplerCube','sampler2DArray'):raise ValueError(f'Unsupported sampler {typ}')
            typ='sampler2dArray' if count>1 or typ=='sampler2DArray' else 'samplerCubemap' if typ=='samplerCube' else 'sampler2d'
            params.append(f'{{type:{typ},name:{p}}}')
        else:params.append('{type:"'+TYPES.get(typ,typ)+(f'[{count}]' if count>1 else '')+'",name:'+p+'}')
    params+=['{type:mat4,name:os_invProjection}','{type:mat4,name:os_projection}','{type:mat4,name:os_normalMatrix}',
             '{type:mat4,name:os_eyeToWorld}','{type:mat4,name:os_worldToEye}',
             '{type:float4,name:os_viewport}','{type:float4,name:os_drawViewport}','{type:float,name:os_instanceID}','{type:float,name:os_writeDepth}']
    if lit:params.append('{type:float4,name:os_surfaceProfile}')
    instanced='samplerBuffer' in v
    if instanced:params += [f'{{type:float4,name:os_instanceData{i}}}' for i in range(6)]
    def transform(source,stage):
        source=unique_locals(source)
        structures=[]
        def protect(match):
            structures.append(match[0]);return f'OS_STRUCT_{len(structures)-1};'
        source=re.sub(r'\bstruct\s+\w+\s*\{[^}]*\}\s*;',protect,source)
        # Keep light structures as dead/default globals. Lighting functions no
        # longer read them; specialized water shaders are audited separately.
        source=DECL.sub(lambda m: 'LightSources '+m[2]+';' if m[1]=='LightSources' else '',source)
        source=re.sub(r'(?m)^\s*invariant\s+\w+\s*;', '',source)
        source=IO.sub(lambda m:m[3]+' '+m[4]+';',source)
        if stage=='v':source=re.sub(r'\bgl_Position\b','os_clip',source);source=source.replace('gl_PointSize','os_pointSize').replace('gl_InstanceID','int(materialParams.os_instanceID)')
        else:source=source.replace('gl_PointCoord','os_pointCoord')
        if instanced and stage=='v':
            source=replace_calls(source,'texelFetch',lambda a:'materialParams.os_instanceData'+str(int(re.search(r'\+\s*(\d+)',a[1])[1]) if '+' in a[1] else 0) if a[0]=='instances' else None)
        # Texture palettes use one array sampler instead of 15 independent
        # samplers, leaving room for Filament shadow/IBL resources.
        def texture(args):
            match=re.fullmatch(r'(\w+)\[([^]]+)\]',args[0])
            if match and match[1] in uniforms and uniforms[match[1]]['count']>1:
                return 'texture(materialParams_'+uniforms[match[1]]['parameter']+',vec3('+args[1]+',float('+match[2]+'))'+(','+','.join(args[2:]) if len(args)>2 else '')+')'
            return None
        source=replace_calls(source,'texture',texture)
        for name,uniform in sorted(uniforms.items(),key=lambda x:-len(x[0])):
            prefix='materialParams_' if uniform['type'].startswith('sampler') else 'materialParams.'
            source=re.sub(r'(?<![.\w])'+re.escape(name)+r'\b',prefix+uniform['parameter'],source)
        source=re.sub(r'\bmain\s*\(','os_main(',source)
        source=re.sub(r'\bfragColor\b','os_fragColor',source)
        source=re.sub(r'\bfog\b','os_legacyFog',source)
        if stage=='v':
            source=re.sub(r'(vec4\s+(osl_main_eyeN)\s*=[^;]+;)',r'\1os_runtimeNormal=\2.xyz;',source)
        for index,structure in enumerate(structures):source=source.replace(f'OS_STRUCT_{index};',structure)
        return source
    vertex=transform(v,'v');fragment=transform(f,'f')
    declarations='vec4 os_clip;float os_pointSize;vec2 os_pointCoord;vec3 os_runtimeNormal;float os_fabricAmount;\n'
    fragment=re.sub(r'(float\s+osl_main_clothes\s*=[^;]+;)',r'\1os_fabricAmount=clamp(osl_main_clothes,0.0,1.0);',fragment)
    vertex=declarations+vertex
    fragment=declarations+fragment
    assign=[]
    for index,attr in enumerate(attrs):assign.append(attr['name']+'='+('getCustom'+str(index)+'()'+('' if attr['size']==4 else '.'+'xyzw'[:attr['size']]))+';')
    normal=next((a for a in attrs if a['name'].startswith('normal')),None)
    vertex+='\nvoid materialVertex(inout MaterialVertexInputs material){\n'+''.join(assign)+'os_pointSize=1.0;os_main();\n'
    if points:
        vertex+='vec2 corner=material.uv1;os_clip.xy+=(corner*2.0-1.0)*os_pointSize/materialParams.os_drawViewport.zw*os_clip.w;\n'
    vertex+='os_clip.xy=os_clip.xy*(materialParams.os_drawViewport.zw/materialParams.os_viewport.xy)+os_clip.w*((materialParams.os_drawViewport.xy*2.0+materialParams.os_drawViewport.zw)/materialParams.os_viewport.xy-1.0);\n'
    vertex+='vec4 eye=materialParams.os_invProjection*os_clip;material.worldPosition=materialParams.os_eyeToWorld*(eye/eye.w);\n'
    if lit:
        fallback='mat3(materialParams.os_normalMatrix)*'+normal['name']+'.xyz' if normal else 'mat3(materialParams.os_normalMatrix)*normalize(position.xyz)' if liquid else 'vec3(0,0,1)'
        vertex+='material.worldNormal=normalize(mat3(materialParams.os_eyeToWorld)*('+('os_runtimeNormal' if 'os_runtimeNormal=' in vertex else fallback)+'));\n'
    lanes=[]
    for varying in varyings:
        for channel in range(varying['size']):
            value=('os_legacyFog' if varying['name']=='fog' else varying['name'])+('.'+'xyzw'[channel] if varying['size']>1 else '')
            lanes.append('float('+value+')' if varying['type'].startswith(('int','uint','bool','ivec')) else value)
    if points:lanes+=['corner.x','1.0-corner.y']
    if self_illum:lanes+=['glowSelfIllum.w']
    for i,value in enumerate(lanes):vertex+=f'material.os{i//4}.{"xyzw"[i%4]}={value};\n'
    vertex+='}\n'
    unpack=''
    for varying in varyings:
        parts=[f'variable_os{(varying["offset"]+i)//4}.{"xyzw"[(varying["offset"]+i)%4]}' for i in range(varying['size'])]
        unpack+=('os_legacyFog' if varying['name']=='fog' else varying['name'])+'='+ (varying['type']+'('+','.join(parts)+')' if len(parts)>1 or varying['type']!='float' else parts[0])+';'
    if points:unpack+='os_pointCoord=vec2('+','.join(f'variable_os{(point_offset+i)//4}.{"xyzw"[(point_offset+i)%4]}' for i in range(2))+');'
    variables=','.join('os'+str(i) for i in range((offset+3)//4))
    names=','.join('custom'+str(i) for i in range(len(attrs)))+(',uv1' if points else '')
    key=vname[:-2]+'__'+fname[:-2]
    for variant in ('opaque','transparent','additive','depth'):
        depth=variant=='depth'
        blend='masked' if variant in ('opaque','depth') else 'add' if variant=='additive' else variant
        settings=f'name:"{key}-{variant}",shadingModel:{"unlit" if depth or not lit else "lit"},vertexDomain:world,flipUV:false,doubleSided:true,blending:{blend},'
        settings+='requires:['+names+'],' if names else ''
        settings+='variables:['+variables+'],' if variables else ''
        settings+='parameters:['+','.join(params)+']'
        body=unpack+'os_fabricAmount=0.0;os_main();'
        # Legacy opaque draws ignore framebuffer alpha. Some body shaders
        # leave glowAlpha.a at zero; their explicit texture cutout has already
        # run inside os_main. Do not add a second, unintended alpha rejection.
        if variant in ('opaque','depth'):body+='os_fragColor.a=1.0;'
        if depth:body+='if(materialParams.os_writeDepth<0.5)discard;vec4 clip=materialParams.os_projection*materialParams.os_worldToEye*vec4(getWorldPosition(),1.0);float z=clip.z/clip.w*0.5+0.5;material.baseColor=vec4(z,z,z,os_fragColor.a);'
        else:
            body+='material.baseColor=os_fragColor;'
            if lit:
                if vname.startswith('Body'):
                    body+='material.roughness=mix(materialParams.os_surfaceProfile.x,materialParams.os_surfaceProfile.w,os_fabricAmount);material.metallic=materialParams.os_surfaceProfile.y*os_fabricAmount;'
                else:body+='material.roughness=materialParams.os_surfaceProfile.x;material.metallic=materialParams.os_surfaceProfile.y;'
                body+='material.reflectance=materialParams.os_surfaceProfile.z;'
            if self_illum:body+=f'material.emissive=vec4(os_fragColor.rgb*variable_os{self_offset//4}.{"xyzw"[self_offset%4]},0.0);'
            if vname.startswith('Lava'):body+='material.emissive=vec4(os_fragColor.rgb,0.0);'
            if 'os_effectApply' in [u['parameter'] for u in uniforms.values()] and 'os_texEffect' in [u['parameter'] for u in uniforms.values()]:
                body+='if(vertTexIndex.y*materialParams.os_effectApply==2.0)material.emissive=vec4(os_fragColor.rgb*texture(materialParams_os_texEffect,vertEffCoord.xy).a,0.0);'
            if fog_factor:
                factor=re.sub(r'\bfog\b','os_legacyFog',fog_factor)
                body+=f'float os_fogAmount=clamp({factor},0.0,1.0);material.postLightingColor=vec4(materialParams.os_fogColor*os_fogAmount,os_fogAmount);'
        extension='#extension GL_EXT_shader_implicit_conversions : enable\n'
        variant_vertex=re.sub(r'material\.worldNormal=[^;]*;','',vertex) if depth else vertex
        content='material{'+settings+'}\nvertex{\n'+extension+variant_vertex+'}\nfragment{\n'+extension+fragment+'\nvoid material(inout MaterialInputs material){prepareMaterial(material);'+body+'}\n}\n'
        (output/(key+'-'+variant+'.mat')).write_text(content)
    return {'key':key,'vertex':vname,'fragment':fname,'attributes':attrs,'uniforms':list(uniforms.values()),'lit':lit,'points':points,'instanced':instanced,'varyingComponents':offset,'sourceSha256':hashlib.sha256((v+f).encode()).hexdigest()}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--only');args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'src/lib/graphics/auglsl.cpp').read_text();pairs=set()
    for block in re.split(r'void AuGLSL\w+::Build\(',source)[1:]:
        names=re.findall(r'AuGLSLShader\s+\w+\([^;]+?GL_(VERTEX|FRAGMENT)_SHADER,\s*"([^"]+)"',block.split('\nvoid ')[0])
        vertex=next((n for t,n in names if t=='VERTEX'),None);fragment=next((n for t,n in names if t=='FRAGMENT'),None)
        if vertex and fragment:pairs.add((vertex,fragment))
    catalog=[];failures=[]
    for vertex in SHADERS.glob('*.v.glsl'):
        fragment=SHADERS/(vertex.name.replace('.v.glsl','.f.glsl'))
        if fragment.exists() and 'Depth' not in vertex.name:pairs.add((vertex.name[:-5],fragment.name[:-5]))
    for v,f in sorted(pairs):
        if args.only and not re.search(args.only,v):continue
        try:catalog.append(convert(v,f,args.output))
        except Exception as error:failures.append({'vertex':v,'fragment':f,'error':str(error)})
    (args.output/'manifest.json').write_text(json.dumps({'version':1,'materials':catalog,'failures':failures},indent=2))
    print(json.dumps({'converted':len(catalog),'failures':failures},indent=2))
    return bool(failures)
if __name__=='__main__':raise SystemExit(main())
