import React, { useState } from 'react';
import { X } from 'lucide-react';

interface SettingsModalProps {
  camera: { id: string; name: string };
  onClose: () => void;
  onSave: (id: string, newName: string) => void;
}

const SettingsModal = ({ camera, onClose, onSave }: SettingsModalProps) => {
  const [newName, setNewName] = useState(camera.name);

  return (
    <div className="fixed inset-0 bg-black/60 backdrop-blur-sm z-[100] flex items-center justify-center p-4">
      <div className="bg-[#111] border border-[#333] rounded-xl shadow-2xl w-full max-w-md overflow-hidden">
        <div className="flex items-center justify-between px-6 py-4 border-b border-[#222]">
          <h3 className="font-bold text-lg">Camera Settings</h3>
          <button onClick={onClose} className="p-1 hover:bg-white/10 rounded-lg transition-colors">
            <X className="w-5 h-5" />
          </button>
        </div>
        
        <div className="p-6 space-y-4">
          <div className="space-y-2">
            <label className="text-xs font-mono uppercase text-gray-500 tracking-wider">Camera Name</label>
            <input 
              type="text" 
              value={newName}
              onChange={(e) => setNewName(e.target.value)}
              className="w-full bg-black border border-[#333] rounded-lg px-4 py-2.5 focus:border-blue-500 outline-none transition-colors font-mono text-sm"
              placeholder="Enter camera name..."
              autoFocus
            />
          </div>
          
          <div className="pt-4 flex gap-3">
            <button 
              onClick={onClose}
              className="flex-1 px-4 py-2.5 rounded-lg border border-[#333] hover:bg-white/5 transition-colors text-sm font-bold"
            >
              Cancel
            </button>
            <button 
              onClick={() => onSave(camera.id, newName)}
              className="flex-1 px-4 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 transition-colors text-sm font-bold shadow-lg shadow-blue-900/20"
            >
              Save Changes
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};

export default SettingsModal;
