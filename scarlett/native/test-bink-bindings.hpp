/* Project-owned inputs. Kept alive until the calling draw fixture's fence. */
struct BinkInputs {
    ComPtr<ID3D12Resource> image[5],buf[3],upload,vb;
    ComPtr<ID3D12DescriptorHeap> table;
    int bind(ID3D12Device* d,ID3D12GraphicsCommandList* l,bool alpha){
        const UINT images=alpha?5:4;
        D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,images+3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
        HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&table)));auto start=table->GetCPUDescriptorHandleForHeapStart();
        auto slot=[&](UINT j){auto h=start;h.ptr+=j*d->GetDescriptorHandleIncrementSize(hd.Type);return h;};
        D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_UPLOAD;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC r={};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=4096;r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto make=[&](ID3D12Resource** out){return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(out));};
        HR(make(upload.GetAddressOf()));r.Width=256;HR(make(vb.GetAddressOf()));
        const float verts[][9]={{-1,-1,0,0,.5,17,0,0,1},{3,-1,0,2,.5,17,0,0,1},{-1,3,0,0,.5,17,0,0,1}};void*m;HR(vb->Map(0,nullptr,&m));memcpy(m,verts,sizeof verts);vb->Unmap(0,nullptr);
        D3D12_VERTEX_BUFFER_VIEW view={vb->GetGPUVirtualAddress(),sizeof verts,sizeof verts[0]};l->IASetVertexBuffers(0,1,&view);
        const UINT values[]={11,13,19};for(UINT j=0;j<3;j++){
            HR(make(buf[j].GetAddressOf()));HR(buf[j]->Map(0,nullptr,&m));memcpy(m,values+j,4);buf[j]->Unmap(0,nullptr);
            if(j){D3D12_CONSTANT_BUFFER_VIEW_DESC c={buf[j]->GetGPUVirtualAddress(),256};d->CreateConstantBufferView(&c,slot(images+j));}
            else{D3D12_SHADER_RESOURCE_VIEW_DESC s={};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.NumElements=1;s.Buffer.StructureByteStride=4;d->CreateShaderResourceView(buf[j].Get(),&s,slot(0));}
        }
        hp.Type=D3D12_HEAP_TYPE_DEFAULT;r.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;r.Format=DXGI_FORMAT_R32_FLOAT;r.Width=2;
        HR(upload->Map(0,nullptr,&m));for(UINT j=0;j<images;j++)for(UINT z=0;z<(j==3?2u:1u);z++){
            float texels[]={float(2+8*j+4*z),float(6+8*j+4*z)};memcpy((BYTE*)m+j*512+z*256,texels,sizeof texels);
        }upload->Unmap(0,nullptr);
        for(UINT j=0;j<images;j++){r.Dimension=j==3?D3D12_RESOURCE_DIMENSION_TEXTURE3D:D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.DepthOrArraySize=j==3?2:1;
            HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&image[j])));
            D3D12_TEXTURE_COPY_LOCATION src={},dst={};dst.pResource=image[j].Get();src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint={j*512ull,{r.Format,2,1,r.DepthOrArraySize,256}};
            l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);D3D12_RESOURCE_BARRIER tr={};tr.Transition={image[j].Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&tr);
            d->CreateShaderResourceView(image[j].Get(),nullptr,slot(alpha && j>=2?(j==4?3:j+2):j+1));
        }
        auto heap=table.Get();l->SetDescriptorHeaps(1,&heap);l->SetGraphicsRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());return 0;
    }
};
