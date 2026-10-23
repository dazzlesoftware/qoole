// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
namespace Genesis.Scripting;
public static unsafe class ScriptBridge
{
    private sealed class ModuleContext() : AssemblyLoadContext(isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName name) => name.Name == typeof(IScriptModule).Assembly.GetName().Name ? typeof(IScriptModule).Assembly : null;
    }
    private sealed record Module(ModuleContext Context, IScriptModule Export);
    private sealed class Session : IScriptHost
    {
        public readonly Dictionary<string, Module> Modules = new(StringComparer.Ordinal);
        public readonly int Owner = Environment.CurrentManagedThreadId;
        public bool Active, Disposed;
        public nint Context;
        public delegate* unmanaged[Cdecl]<nint,byte*,int,byte**,int*,int> HostInvoke;
        public delegate* unmanaged[Cdecl]<nint,byte*,void> HostRelease;
        public JsonElement Invoke(string api, JsonElement arguments)
        {
            if(Disposed || !Active || Owner != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Script host calls require an active call on the owning thread.");
            var request=JsonSerializer.SerializeToUtf8Bytes(new {API=api, Arguments=arguments});
            if(request.Length>4*1024*1024)throw new InvalidOperationException("Host request exceeds 4 MiB.");
            byte* response=null;int size=0;
            fixed(byte* data=request)
            {
                var status=HostInvoke(Context,data,request.Length,&response,&size);
                try{
                    if(response==null || size<0 || size>4*1024*1024)throw new InvalidOperationException("Invalid native host response.");
                    var bytes=new ReadOnlySpan<byte>(response,size);
                    if(status!=0)throw new InvalidOperationException(Encoding.UTF8.GetString(bytes));
                    using var doc=JsonDocument.Parse(bytes.ToArray());return doc.RootElement.Clone();
                }finally{if(response!=null)HostRelease(Context,response);}
            }
        }
        public void Close(){
            Disposed=true;
            Exception? failure=null;
            foreach(var module in Modules.Values){try{if(module.Export is IDisposable disposable)disposable.Dispose();}catch(Exception error){failure??=error;}finally{module.Context.Unload();}}
            Modules.Clear();if(failure!=null)throw new InvalidOperationException("Script module disposal failed.",failure);
        }
    }
    private static readonly Dictionary<long,Session> Sessions=[];
    private static long NextSession;
    private static JsonElement Null()=>JsonSerializer.SerializeToElement((object?)null);
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvCdecl)])]
    public static int Dispatch(byte* request,int requestSize,byte* payload,int payloadSize,byte** output,int* outputSize)
    {
        if(output==null||outputSize==null)return -1;*output=null;*outputSize=0;
        try{
            if(request==null||requestSize<0||requestSize>4*1024*1024||payloadSize<0||payloadSize>4*1024*1024)throw new InvalidOperationException("Invalid script bridge request.");
            using var doc=JsonDocument.Parse(new ReadOnlySpan<byte>(request,requestSize).ToArray());var root=doc.RootElement;
            var op=root.GetProperty("Operation").GetString();JsonElement result;
            lock(Sessions){
                if(op=="Create"){
                    var session=new Session{Context=(nint)long.Parse(root.GetProperty("Context").GetString()!),HostInvoke=(delegate* unmanaged[Cdecl]<nint,byte*,int,byte**,int*,int>)(nint)long.Parse(root.GetProperty("HostInvoke").GetString()!),HostRelease=(delegate* unmanaged[Cdecl]<nint,byte*,void>)(nint)long.Parse(root.GetProperty("HostRelease").GetString()!)};
                    var id=++NextSession;Sessions.Add(id,session);result=JsonSerializer.SerializeToElement(id);
                }else{
                    var id=root.GetProperty("Session").GetInt64();if(!Sessions.TryGetValue(id,out var session))throw new InvalidOperationException("Script session no longer exists.");
                    if(session.Owner!=Environment.CurrentManagedThreadId||session.Active)throw new InvalidOperationException("Script session calls must be non-reentrant and on the owning thread.");
                    session.Active=true;
                    try{
                        if(op=="Dispose"){Sessions.Remove(id);session.Close();result=Null();}
                        else if(op=="Load"){
                            var name=root.GetProperty("Module").GetString()!;if(!session.Modules.ContainsKey(name)&&session.Modules.Count>=256)throw new InvalidOperationException("Script module limit exceeded.");
                            var context=new ModuleContext();
                            try{
                                using var stream=new MemoryStream(new ReadOnlySpan<byte>(payload,payloadSize).ToArray());var assembly=context.LoadFromStream(stream);
                                var exports=assembly.GetTypes().Where(t=>t.IsPublic&&!t.IsAbstract&&typeof(IScriptModule).IsAssignableFrom(t)).ToArray();
                                if(exports.Length!=1)throw new InvalidOperationException("A script DLL must export exactly one public IScriptModule implementation.");
                                var module=new Module(context,(IScriptModule)Activator.CreateInstance(exports[0])!);
                                if(session.Modules.TryGetValue(name,out var old)&&old.Export is IDisposable disposable)disposable.Dispose();
                                session.Modules[name]=module;old?.Context.Unload();result=Null();
                            }catch{context.Unload();throw;}
                        }else if(op=="Call"){
                            var name=root.GetProperty("Module").GetString()!;var function=root.GetProperty("Function").GetString()!;
                            result=session.Modules[name].Export.Call(function,root.GetProperty("Arguments").Clone(),session);
                        }else throw new InvalidOperationException("Unknown script bridge operation.");
                    }finally{session.Active=false;}
                }
            }
            return Write(new {Success=true,Result=result},output,outputSize);
        }catch(Exception error){
            while(error is TargetInvocationException invocation&&invocation.InnerException!=null)error=invocation.InnerException;
            return Write(new {Success=false,Error=error.Message},output,outputSize);
        }
    }
    private static int Write<T>(T value,byte** output,int* size)
    {
        try{
            var bytes=JsonSerializer.SerializeToUtf8Bytes(value);
            if(bytes.Length>4*1024*1024)bytes=JsonSerializer.SerializeToUtf8Bytes(new {Success=false,Error="Script response exceeds 4 MiB."});
            *output=(byte*)NativeMemory.Alloc((nuint)Math.Max(1,bytes.Length));if(*output==null)return -1;
            bytes.CopyTo(new Span<byte>(*output,bytes.Length));*size=bytes.Length;return 0;
        }catch{return -1;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvCdecl)])]
    public static void FreeOutput(byte* output)=>NativeMemory.Free(output);
}
