; CPU-only boundary functions. Unknown resources and DX opcodes trap.
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
@inputs = global [3 x [4 x float]] zeroinitializer
@outputs = global [3 x [4 x float]] zeroinitializer
@object = global [16 x float] zeroinitializer
@camera = global [128 x float] zeroinitializer
@texture = global [8 x [8 x [4 x float]]] zeroinitializer
declare void @llvm.trap()
declare float @llvm.fabs.f32(float)
declare float @llvm.fma.f32(float,float,float)
declare float @llvm.floor.f32(float)
define void @check(i1 %ok) {
  br i1 %ok, label %done, label %bad
bad:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
define i32 @dx.op.createHandle(i32 %op,i8 %class,i32 %range,i32 %index,i1 %vary) {
  ret i32 %index
}
define float @dx.op.loadInput.f32(i32 %op,i32 %reg,i32 %row,i8 %col,i32 %axis) {
  %p = getelementptr [4 x float],ptr @inputs,i32 %reg,i8 %col
  %v = load float,ptr %p
  ret float %v
}
define void @dx.op.storeOutput.f32(i32 %op,i32 %reg,i32 %row,i8 %col,float %v) {
  %p = getelementptr [4 x float],ptr @outputs,i32 %reg,i8 %col
  store float %v,ptr %p
  ret void
}
define %dx.types.ResRet.f32 @rgba(ptr %p) {
  %p1 = getelementptr float,ptr %p,i32 1
  %p2 = getelementptr float,ptr %p,i32 2
  %p3 = getelementptr float,ptr %p,i32 3
  %a = load float,ptr %p
  %b = load float,ptr %p1
  %c = load float,ptr %p2
  %d = load float,ptr %p3
  %s = insertvalue %dx.types.ResRet.f32 undef,float %a,0
  %t = insertvalue %dx.types.ResRet.f32 %s,float %b,1
  %u = insertvalue %dx.types.ResRet.f32 %t,float %c,2
  %v = insertvalue %dx.types.ResRet.f32 %u,float %d,3
  ret %dx.types.ResRet.f32 %v
}
define %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 %op,i32 %handle,i32 %index,i32 %offset) {
  %a = icmp eq i32 %handle,7
  %b = icmp eq i32 %index,0
  %c = icmp ule i32 %offset,48
  %d = and i1 %a,%b
  %e = and i1 %c,%d
  call void @check(i1 %e)
  %p = getelementptr i8,ptr @object,i32 %offset
  %r = call %dx.types.ResRet.f32 @rgba(ptr %p)
  ret %dx.types.ResRet.f32 %r
}
define %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 %op,i32 %handle,i32 %row) {
  %a = icmp eq i32 %handle,0
  %b = icmp ult i32 %row,32
  %ok = and i1 %a,%b
  call void @check(i1 %ok)
  %p = getelementptr [4 x float],ptr @camera,i32 %row
  %r = load %dx.types.CBufRet.f32,ptr %p
  ret %dx.types.CBufRet.f32 %r
}
define i32 @texel(float %coord) {
  ; Isotropic 1:1 footprint at exact texel centres, single mip, wrap.
  %base = call float @llvm.floor.f32(float %coord)
  %f = fsub float %coord,%base
  %t = fmul float %f,8.0
  %i = fptoui float %t to i32
  %int = uitofp i32 %i to float
  %center = fadd float %int,0.5
  %ok = fcmp oeq float %t,%center
  call void @check(i1 %ok)
  ret i32 %i
}
define %dx.types.ResRet.f32 @dx.op.sample.f32(i32 %op,i32 %srv,i32 %sampler,float %u,float %v,float %c,float %d,i32 %ox,i32 %oy,i32 %oz,float %clamp) {
  %a = icmp eq i32 %srv,277
  %b = icmp eq i32 %sampler,14
  %off = or i32 %ox,%oy
  %z = icmp eq i32 %off,0
  %ab = and i1 %a,%b
  %ok = and i1 %ab,%z
  call void @check(i1 %ok)
  %x = call i32 @texel(float %u)
  %y = call i32 @texel(float %v)
  %p = getelementptr [8 x [4 x float]],ptr @texture,i32 %y,i32 %x
  %r = call %dx.types.ResRet.f32 @rgba(ptr %p)
  ret %dx.types.ResRet.f32 %r
}
define float @dx.op.unary.f32(i32 %op,float %v) {
  %ok = icmp eq i32 %op,6
  call void @check(i1 %ok)
  %r = call float @llvm.fabs.f32(float %v)
  ret float %r
}
define float @dx.op.tertiary.f32(i32 %op,float %a,float %b,float %c) {
  %ok = icmp eq i32 %op,46
  call void @check(i1 %ok)
  %r = call float @llvm.fma.f32(float %a,float %b,float %c)
  ret float %r
}
define i32 @dx.op.legacyF32ToF16(i32 %op,float %v) {
  %h = fptrunc float %v to half
  %b = bitcast half %h to i16
  %i = zext i16 %b to i32
  ret i32 %i
}
define float @dx.op.legacyF16ToF32(i32 %op,i32 %v) {
  %b = trunc i32 %v to i16
  %h = bitcast i16 %b to half
  %f = fpext half %h to float
  ret float %f
}
