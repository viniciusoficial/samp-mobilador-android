package com.sampmobile.launcher.ui.adapter;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;

import java.util.List;

import com.sampmobile.launcher.R;

@Obfuscate
public class PlayersAdapter extends RecyclerView.Adapter<PlayersAdapter.ViewHolder> {

    private final List<String[]> players;

    static class ViewHolder extends RecyclerView.ViewHolder {
        TextView player_name;
        TextView player_score;
        TextView player_ping;

        public ViewHolder(View itemView) {
            super(itemView);
            player_name = itemView.findViewById(R.id.player_name);
            player_score = itemView.findViewById(R.id.player_score);
            player_ping = itemView.findViewById(R.id.player_ping);
        }
    }

    public PlayersAdapter(List<String[]> players) {
        this.players = players;
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.layout_player_item, parent, false);
        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
        String[] player = players.get(position);
        holder.player_name.setText(player[1]);
        holder.player_score.setText(player[2]);
        holder.player_ping.setText(player[3]);
    }

    @Override
    public int getItemCount() {
        return players.size();
    }
}
